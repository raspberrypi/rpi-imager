/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * macOS drive enumeration using DiskArbitration and IOKit.
 *
 * Design notes:
 * - Uses DiskArbitration for disk enumeration and property queries
 * - Uses IOKit for APFS parent device discovery
 * - Uses NSFileManager for mount point enumeration
 * - Separates disk enumeration from property extraction for testability
 * - Uses os_log for unified logging (Console.app integration)
 */

#include "drivelist.h"

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#import <DiskArbitration/DiskArbitration.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/storage/IOMedia.h>
#import <IOKit/IOBSD.h>
#include <os/log.h>
#include <sys/sysctl.h>

// Unified logging for disk operations - visible in Console.app
// Users can capture with: log collect --device --last 1h
static os_log_t drivelist_log() {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("com.raspberrypi.imager", "drivelist");
    });
    return log;
}

#define DRIVELIST_LOG_INFO(fmt, ...) os_log_info(drivelist_log(), fmt, ##__VA_ARGS__)
#define DRIVELIST_LOG_ERROR(fmt, ...) os_log_error(drivelist_log(), fmt, ##__VA_ARGS__)
#define DRIVELIST_LOG_DEBUG(fmt, ...) os_log_debug(drivelist_log(), fmt, ##__VA_ARGS__)

namespace Drivelist {

namespace {

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Check if a BSD name represents a partition (vs whole disk)
 * @param diskName BSD name like "disk0" or "disk0s1"
 * @return true if this is a partition
 */
bool isPartition(NSString* diskName)
{
    // Partitions match pattern: disk<N>s<M>
    NSPredicate* predicate = [NSPredicate predicateWithFormat:@"SELF MATCHES %@", @"disk\\d+s\\d+"];
    return [predicate evaluateWithObject:diskName];
}

/**
 * @brief Detect if a disk is an SD card by checking its icon
 *
 * macOS assigns SD.icns to SD card media, which is more reliable than
 * checking the protocol name.
 */
bool isSDCard(CFDictionaryRef diskDescription)
{
    CFDictionaryRef mediaIconDict = static_cast<CFDictionaryRef>(
        CFDictionaryGetValue(diskDescription, kDADiskDescriptionMediaIconKey));

    if (!mediaIconDict) return false;

    CFStringRef iconFileKey = CFStringCreateWithCString(nullptr, "IOBundleResourceFile", kCFStringEncodingUTF8);
    CFStringRef iconFileName = static_cast<CFStringRef>(CFDictionaryGetValue(mediaIconDict, iconFileKey));
    CFRelease(iconFileKey);

    if (!iconFileName) return false;

    return [(__bridge NSString*)iconFileName isEqualToString:@"SD.icns"];
}

/**
 * @brief Get NSNumber from CFDictionary safely
 */
NSNumber* getNumber(CFDictionaryRef dict, const void* key)
{
    return (__bridge NSNumber*)CFDictionaryGetValue(dict, key);
}

/**
 * @brief Get NSString from CFDictionary safely
 */
NSString* getString(CFDictionaryRef dict, const void* key)
{
    return (__bridge NSString*)CFDictionaryGetValue(dict, key);
}

/**
 * @brief Find the physical parent device of an APFS volume
 *
 * APFS volumes appear as separate disks but are actually containers
 * on a physical disk. We need to find the physical parent to properly
 * attribute mountpoints.
 *
 * @param bsdName BSD name of the APFS volume (e.g., "disk2")
 * @return BSD name of the physical parent, or empty string if not found
 */
std::string findAPFSParent(const char* bsdName)
{
    std::string result;
    
    @autoreleasepool {
        io_service_t service = IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOBSDNameMatching(kIOMainPortDefault, 0, bsdName));

        if (!service) {
            DRIVELIST_LOG_DEBUG("findAPFSParent: no IOService for %{public}s", bsdName);
            return result;
        }

        // Navigate up the IORegistry hierarchy to find the physical media
        // APFS structure: APFSVolume -> APFSContainer -> AppleAPFSContainerScheme -> IOMedia
        io_service_t current = service;
        io_service_t toRelease = IO_OBJECT_NULL;  // Track object to release

        for (int i = 0; i < 3; i++) {
            io_service_t parent = IO_OBJECT_NULL;
            kern_return_t kr = IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent);

            // Release previous 'current' if it's not the original service
            if (toRelease != IO_OBJECT_NULL) {
                IOObjectRelease(toRelease);
                toRelease = IO_OBJECT_NULL;
            }

            if (kr != KERN_SUCCESS) {
                DRIVELIST_LOG_DEBUG("findAPFSParent: IORegistry traversal failed at level %d (kr=%#x)", i, kr);
                if (current != service) {
                    IOObjectRelease(current);
                }
                IOObjectRelease(service);
                return result;
            }

            toRelease = current;  // Mark current for release on next iteration
            current = parent;
        }

        // Release the last intermediate object
        if (toRelease != IO_OBJECT_NULL && toRelease != service) {
            IOObjectRelease(toRelease);
        }

        // Now look for sibling that is IOMedia class
        io_iterator_t iterator = IO_OBJECT_NULL;
        kern_return_t kr = IORegistryEntryGetParentIterator(current, kIOServicePlane, &iterator);
        
        if (kr != KERN_SUCCESS || iterator == IO_OBJECT_NULL) {
            DRIVELIST_LOG_DEBUG("findAPFSParent: failed to get parent iterator (kr=%#x)", kr);
            IOObjectRelease(current);
            IOObjectRelease(service);
            return result;
        }

        io_service_t parent;
        while ((parent = IOIteratorNext(iterator))) {
            if (IOObjectConformsTo(parent, kIOMediaClass)) {
                CFTypeRef bsdNameRef = IORegistryEntryCreateCFProperty(
                    parent, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);

                if (bsdNameRef && CFGetTypeID(bsdNameRef) == CFStringGetTypeID()) {
                    NSString* name = (__bridge NSString*)bsdNameRef;
                    result = [name UTF8String];
                    DRIVELIST_LOG_DEBUG("findAPFSParent: %{public}s -> %{public}s", bsdName, result.c_str());
                }
                if (bsdNameRef) {
                    CFRelease(bsdNameRef);
                }
            }
            IOObjectRelease(parent);
            if (!result.empty()) break;
        }

        IOObjectRelease(iterator);
        IOObjectRelease(current);
        IOObjectRelease(service);
    }

    return result;
}

/**
 * @brief Create a DeviceDescriptor from DiskArbitration properties
 */
DeviceDescriptor createDeviceDescriptor(const std::string& bsdName, CFDictionaryRef diskDescription)
{
    DeviceDescriptor device;

    NSString* protocol = getString(diskDescription, kDADiskDescriptionDeviceProtocolKey);
    NSNumber* blockSize = getNumber(diskDescription, kDADiskDescriptionMediaBlockSizeKey);
    bool internal = [getNumber(diskDescription, kDADiskDescriptionDeviceInternalKey) boolValue];
    bool removable = [getNumber(diskDescription, kDADiskDescriptionMediaRemovableKey) boolValue];
    bool ejectable = [getNumber(diskDescription, kDADiskDescriptionMediaEjectableKey) boolValue];
    bool writable = [getNumber(diskDescription, kDADiskDescriptionMediaWritableKey) boolValue];

    // Device paths
    device.device = "/dev/" + bsdName;
    device.raw = "/dev/r" + bsdName;

    // Description - sanitize to prevent Unicode display attacks
    NSString* mediaName = getString(diskDescription, kDADiskDescriptionMediaNameKey);
    device.description = mediaName ? sanitizeForDisplay([mediaName UTF8String]) : "";

    // Device path (bus path)
    NSString* busPath = getString(diskDescription, kDADiskDescriptionBusPathKey);
    if (busPath) {
        device.devicePath = [busPath UTF8String];
        device.devicePathNull = false;
    }

    // Size and block size
    device.size = [getNumber(diskDescription, kDADiskDescriptionMediaSizeKey) unsignedLongLongValue];
    device.blockSize = blockSize ? [blockSize unsignedIntValue] : 512;
    device.logicalBlockSize = device.blockSize;

    // Bus type
    device.busType = protocol ? [protocol UTF8String] : "";
    device.busVersionNull = true;

    // Connection type flags
    device.isUSB = protocol && [protocol isEqualToString:@"USB"];
    device.isCard = isSDCard(diskDescription);
    device.isVirtual = protocol && [protocol isEqualToString:@"Virtual Interface"];

    // SCSI-like protocols (for icon selection)
    NSArray* scsiProtocols = @[@"SATA", @"SCSI", @"ATA", @"IDE", @"PCI", @"NVMe"];
    device.isSCSI = protocol && [scsiProtocols containsObject:protocol];

    // Device state
    device.isReadOnly = !writable;
    device.isRemovable = removable || ejectable;

    // System drive determination:
    // - Internal devices that aren't removable and aren't SD cards
    // - SD cards should never be system drives even in internal readers
    device.isSystem = internal && !removable && !device.isCard;

    // UAS detection not easily available on macOS
    device.isUAS = false;
    device.isUASNull = true;

    // APFS volumes are virtual and need parent device tracking
    if (device.description == "AppleAPFSMedia") {
        device.isVirtual = true;
        device.parentDevice = findAPFSParent(bsdName.c_str());
    }

    return device;
}

/**
 * @brief Callback for DiskArbitration disk enumeration
 */
void diskAppearedCallback(DADiskRef disk, void* context)
{
    auto* diskList = static_cast<NSMutableArray*>(context);
    const char* bsdName = DADiskGetBSDName(disk);
    if (bsdName) {
        [diskList addObject:[NSString stringWithUTF8String:bsdName]];
    }
}

/**
 * @brief Enumerate all disks using DiskArbitration
 * @param outDisks Output array to populate with BSD names
 * @return true on success
 *
 * Note: Uses a short run loop timeout. DiskArbitration callbacks are
 * typically very fast, but on systems with many disks this could
 * theoretically miss some. In practice, 50ms is sufficient.
 */
bool enumerateDisks(NSMutableArray* outDisks)
{
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (!session) {
        return false;
    }

    // Register callback and run loop briefly to collect disks
    DARegisterDiskAppearedCallback(session, nullptr, diskAppearedCallback, (__bridge void*)outDisks);

    CFRunLoopRef runLoop = [[NSRunLoop currentRunLoop] getCFRunLoop];
    DASessionScheduleWithRunLoop(session, runLoop, kCFRunLoopDefaultMode);
    CFRunLoopStop(runLoop);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, NO);

    DAUnregisterCallback(session, reinterpret_cast<void*>(diskAppearedCallback), (__bridge void*)outDisks);
    CFRelease(session);

    // Sort for consistent ordering
    [outDisks sortUsingSelector:@selector(localizedCaseInsensitiveCompare:)];

    return true;
}

/**
 * @brief Add mount point information to device list
 */
void addMountPoints(std::vector<DeviceDescriptor>& deviceList, DASessionRef session)
{
    NSArray* volumeKeys = @[NSURLVolumeNameKey, NSURLVolumeLocalizedNameKey];
    NSArray* volumeURLs = [[NSFileManager defaultManager]
        mountedVolumeURLsIncludingResourceValuesForKeys:volumeKeys
        options:0];

    for (NSURL* volumeURL in volumeURLs) {
        DADiskRef disk = DADiskCreateFromVolumePath(
            kCFAllocatorDefault, session, (__bridge CFURLRef)volumeURL);

        if (!disk) continue;

        const char* bsdNameCStr = DADiskGetBSDName(disk);
        if (!bsdNameCStr) {
            CFRelease(disk);
            continue;
        }

        // Get volume name
        NSString* volumeName = nil;
        [volumeURL getResourceValue:&volumeName forKey:NSURLVolumeLocalizedNameKey error:nil];

        std::string partitionBsdName = bsdNameCStr;

        // Extract disk name from partition name (disk0s1 -> disk0)
        // Find "s" after position 5 to handle names like "disk10s1"
        size_t sPos = partitionBsdName.find('s', 5);
        std::string diskBsdName = (sPos != std::string::npos)
            ? partitionBsdName.substr(0, sPos)
            : partitionBsdName;

        std::string childDevice;

        // Check if this is an APFS volume - if so, attribute to parent
        for (auto& device : deviceList) {
            if (device.device == "/dev/" + diskBsdName) {
                if (!device.parentDevice.empty()) {
                    childDevice = diskBsdName;
                    diskBsdName = device.parentDevice;
                }
                break;
            }
        }

        // Find the target device and add mountpoint
        for (auto& device : deviceList) {
            if (device.device == "/dev/" + diskBsdName) {
                device.mountpoints.push_back([[volumeURL path] UTF8String]);
                device.mountpointLabels.push_back(volumeName ? [volumeName UTF8String] : "");

                if (!childDevice.empty()) {
                    device.childDevices.push_back("/dev/" + childDevice);
                }
                break;
            }
        }

        CFRelease(disk);
    }
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

std::vector<DeviceDescriptor> ListStorageDevices()
{
    std::vector<DeviceDescriptor> deviceList;
    
    @autoreleasepool {
        // Log macOS version for debugging
        NSOperatingSystemVersion osVersion = [[NSProcessInfo processInfo] operatingSystemVersion];
        DRIVELIST_LOG_INFO("ListStorageDevices: macOS %ld.%ld.%ld",
                           (long)osVersion.majorVersion,
                           (long)osVersion.minorVersion,
                           (long)osVersion.patchVersion);

        DASessionRef session = DASessionCreate(kCFAllocatorDefault);
        if (!session) {
            DRIVELIST_LOG_ERROR("ListStorageDevices: failed to create DiskArbitration session");
            // Return sentinel error device so UI can display failure
            DeviceDescriptor errorDevice;
            errorDevice.device = "__error__";
            errorDevice.error = "Failed to enumerate drives: DiskArbitration session creation failed";
            errorDevice.description = "Drive enumeration failed";
            deviceList.push_back(std::move(errorDevice));
            return deviceList;
        }

        // Enumerate all disks
        NSMutableArray* diskNames = [[NSMutableArray alloc] init];
        if (!enumerateDisks(diskNames)) {
            DRIVELIST_LOG_ERROR("ListStorageDevices: disk enumeration failed");
            CFRelease(session);
            DeviceDescriptor errorDevice;
            errorDevice.device = "__error__";
            errorDevice.error = "Failed to enumerate drives: DiskArbitration enumeration failed";
            errorDevice.description = "Drive enumeration failed";
            deviceList.push_back(std::move(errorDevice));
            return deviceList;
        }
        
        DRIVELIST_LOG_INFO("ListStorageDevices: found %lu disk entries", (unsigned long)[diskNames count]);
        
        // Reserve capacity to avoid reallocations during enumeration
        deviceList.reserve(static_cast<size_t>([diskNames count]));

        // Process each whole disk (skip partitions)
        for (NSString* diskName in diskNames) {
            @autoreleasepool {
                if (isPartition(diskName)) {
                    continue;
                }

                std::string bsdName = [diskName UTF8String];
                DADiskRef disk = DADiskCreateFromBSDName(
                    kCFAllocatorDefault, session, bsdName.c_str());

                if (!disk) {
                    DRIVELIST_LOG_DEBUG("ListStorageDevices: skipping %{public}s (no DADiskRef)", bsdName.c_str());
                    continue;
                }

                CFDictionaryRef diskDescription = DADiskCopyDescription(disk);
                if (!diskDescription) {
                    DRIVELIST_LOG_DEBUG("ListStorageDevices: skipping %{public}s (no description)", bsdName.c_str());
                    CFRelease(disk);
                    continue;
                }

                DeviceDescriptor device = createDeviceDescriptor(bsdName, diskDescription);
                DRIVELIST_LOG_DEBUG("ListStorageDevices: %{public}s size=%llu removable=%d system=%d",
                                    bsdName.c_str(), device.size, device.isRemovable, device.isSystem);
                deviceList.push_back(std::move(device));

                CFRelease(diskDescription);
                CFRelease(disk);
            }
        }

        // Add mount point information
        addMountPoints(deviceList, session);

        CFRelease(session);
        // Note: diskNames will be released by ARC when leaving @autoreleasepool
        // If building with MRC, uncomment: [diskNames release];
    }

    DRIVELIST_LOG_INFO("ListStorageDevices: returning %zu devices", deviceList.size());
    return deviceList;
}

} // namespace Drivelist
