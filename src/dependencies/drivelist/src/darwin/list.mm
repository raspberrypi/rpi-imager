/*
 * Copyright 2019 balena.io
 * Copyright 2018 Robin Andersson <me@robinwassen.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <nan.h>
#include "../drivelist.hpp"

#import "REDiskList.h"
#import <Cocoa/Cocoa.h>
#import <DiskArbitration/DiskArbitration.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/storage/IOMedia.h>
#import <IOKit/IOBSD.h>

namespace Drivelist {
  bool IsDiskPartition(NSString *disk) {
    NSPredicate *partitionRegEx = [NSPredicate predicateWithFormat:@"SELF MATCHES %@", @"disk\\d+s\\d+"];
    return [partitionRegEx evaluateWithObject:disk];
  }

  bool IsCard(CFDictionaryRef diskDescription) {
    CFDictionaryRef mediaIconDict = (CFDictionaryRef)CFDictionaryGetValue(
      diskDescription,
      kDADiskDescriptionMediaIconKey
    );
    if (mediaIconDict == nil) {
      return false;
    }

    CFStringRef iconFileNameKeyRef = CFStringCreateWithCString(NULL, "IOBundleResourceFile", kCFStringEncodingUTF8);
    CFStringRef iconFileNameRef = (CFStringRef)CFDictionaryGetValue(mediaIconDict, iconFileNameKeyRef);
    CFRelease(iconFileNameKeyRef);

    if (iconFileNameRef == nil) {
      return false;
    }

    // macOS 10.14.3 - External SD card reader provides `Removable.icns`, not `SD.icns`.
    // But we can't use it to detect SD card, because external drive has `Removable.icns` as well.
    return [(NSString *)iconFileNameRef isEqualToString:@"SD.icns"];
  }

  NSNumber *DictionaryGetNumber(CFDictionaryRef dict, const void *key) {
    return (NSNumber*)CFDictionaryGetValue(dict, key);
  }

  std::string GetParentOfAPFS(const char *diskBsdName) {
      /* Inspired by: https://opensource.apple.com/source/bless/bless-152/libbless/APFS/BLAPFSUtilities.c.auto.html
         Simplified, assumes APFS only has a single physical drive */
      std::string result;
      kern_return_t kret;
      io_iterator_t psIter;
      CFTypeRef data;
      NSString *s;
      io_service_t parent, p = IOServiceGetMatchingService(kIOMasterPortDefault, IOBSDNameMatching(kIOMasterPortDefault, 0, diskBsdName));

      if (p)
      {
          /* Go three levels up in hierarchy */
          for (int i=0; i<3; i++)
          {
              kret = IORegistryEntryGetParentEntry(p, kIOServicePlane, &parent);
              IOObjectRelease(p);
              if (kret)
              {
                  /* Error. Return empty string */
                  return result;
              }
              p = parent;
          }

          IORegistryEntryGetParentIterator(p, kIOServicePlane, &psIter);
          parent = IOIteratorNext(psIter);
          if (parent)
          {
              if (IOObjectConformsTo(parent, kIOMediaClass))
              {
                  data = IORegistryEntryCreateCFProperty(parent, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);

                  if (data && CFGetTypeID(data) == CFStringGetTypeID())
                  {
                      s = (NSString *) data;
                      result = std::string([s UTF8String]);
                      CFRelease(data);
                  }
              }
              IOObjectRelease(parent);
          }
          IOObjectRelease(psIter);
          IOObjectRelease(p);
      }

      return result;
  }

  DeviceDescriptor CreateDeviceDescriptorFromDiskDescription(std::string diskBsdName, CFDictionaryRef diskDescription) {
    NSString *deviceProtocol = (NSString*)CFDictionaryGetValue(diskDescription, kDADiskDescriptionDeviceProtocolKey);
    NSNumber *blockSize = DictionaryGetNumber(diskDescription, kDADiskDescriptionMediaBlockSizeKey);
    bool isInternal = [DictionaryGetNumber(diskDescription, kDADiskDescriptionDeviceInternalKey) boolValue];
    bool isRemovable = [DictionaryGetNumber(diskDescription, kDADiskDescriptionMediaRemovableKey) boolValue];
    bool isEjectable = [DictionaryGetNumber(diskDescription, kDADiskDescriptionMediaEjectableKey) boolValue];

    DeviceDescriptor device = DeviceDescriptor();
    device.enumerator = "DiskArbitration";
    device.busType = (deviceProtocol != nil) ? [deviceProtocol UTF8String] : "";
    device.busVersion = "";
    device.busVersionNull = true;
    device.device = "/dev/" + diskBsdName;
    NSString *devicePath = (NSString*)CFDictionaryGetValue(diskDescription, kDADiskDescriptionBusPathKey);
    device.devicePath = (devicePath != nil) ? [devicePath UTF8String] : "";
    device.raw = "/dev/r" + diskBsdName;
    NSString *description = (NSString*)CFDictionaryGetValue(diskDescription, kDADiskDescriptionMediaNameKey);
    device.description = (description != nil) ? [description UTF8String] : "";
    device.error = "";
    // NOTE: Not sure if kDADiskDescriptionMediaBlockSizeKey returns
    // the physical or logical block size since both values are equal
    // on my machine
    //
    // The can be checked with the following command:
    //      diskutil info / | grep "Block Size"
    device.blockSize = [blockSize unsignedIntValue];
    device.logicalBlockSize = [blockSize unsignedIntValue];
    device.size = [DictionaryGetNumber(diskDescription, kDADiskDescriptionMediaSizeKey) unsignedLongValue];
    device.isReadOnly = ![DictionaryGetNumber(diskDescription, kDADiskDescriptionMediaWritableKey) boolValue];
    device.isSystem = isInternal && !isRemovable;
    device.isVirtual = ((deviceProtocol != nil) && [deviceProtocol isEqualToString:@"Virtual Interface"]);
    device.isRemovable = isRemovable || isEjectable;
    device.isCard = IsCard(diskDescription);
    // NOTE(robin): Not convinced that these bus types should result
    // in device.isSCSI = true, it is rather "not usb or sd drive" bool
    // But the old implementation was like this so kept it this way
    NSArray *scsiTypes = [NSArray arrayWithObjects:@"SATA", @"SCSI", @"ATA", @"IDE", @"PCI", nil];
    device.isSCSI = ((deviceProtocol != nil) && [scsiTypes containsObject:deviceProtocol]);
    device.isUSB = ((deviceProtocol != nil) && [deviceProtocol isEqualToString:@"USB"]);
    device.isUAS = false;
    device.isUASNull = true;

    return device;
  }

  std::vector<DeviceDescriptor> ListStorageDevices() {
    std::vector<DeviceDescriptor> deviceList;

    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (session == nil) {
      return deviceList;
    }

    REDiskList *dl = [[REDiskList alloc] init];
    for (NSString* diskBsdName in dl.disks) {
      if (IsDiskPartition(diskBsdName)) {
        continue;
      }

      std::string diskBsdNameStr = [diskBsdName UTF8String];
      DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, diskBsdNameStr.c_str());
      if (disk == nil) {
        continue;
      }

      CFDictionaryRef diskDescription = DADiskCopyDescription(disk);
      if (diskDescription == nil) {
        CFRelease(disk);
        continue;
      }

      DeviceDescriptor device = CreateDeviceDescriptorFromDiskDescription(diskBsdNameStr, diskDescription);

      if (device.description == "AppleAPFSMedia")
      {
          device.isVirtual = true;
          device.parentDevice = GetParentOfAPFS(diskBsdNameStr.c_str());
      }

      deviceList.push_back(device);

      CFRelease(diskDescription);
      CFRelease(disk);
    }
    [dl release];

    // Add mount points
    NSArray *volumeKeys = [NSArray arrayWithObjects:NSURLVolumeNameKey, NSURLVolumeLocalizedNameKey, nil];
    NSArray *volumePaths = [
      [NSFileManager defaultManager]
      mountedVolumeURLsIncludingResourceValuesForKeys:volumeKeys
      options:0
    ];

    for (NSURL *path in volumePaths) {
      DADiskRef disk = DADiskCreateFromVolumePath(kCFAllocatorDefault, session, (__bridge CFURLRef)path);
      if (disk == nil) {
        continue;
      }

      const char *bsdnameChar = DADiskGetBSDName(disk);
      if (bsdnameChar == nil) {
        CFRelease(disk);
        continue;
      }

      NSString *volumeName;
      [path getResourceValue:&volumeName forKey:NSURLVolumeLocalizedNameKey error:nil];

      std::string partitionBsdName = std::string(bsdnameChar);
      std::string diskBsdName = partitionBsdName.substr(0, partitionBsdName.find("s", 5));
      std::string childDevice;

      /* Check if it concerns APFS volume first, and if so attribute mountpoints to parent device instead */
      for(std::vector<int>::size_type i = 0; i != deviceList.size(); i++) {
        DeviceDescriptor *dd = &deviceList[i];

        if (dd->device == "/dev/" + diskBsdName) {
            if (!dd->parentDevice.empty()) {
                childDevice = diskBsdName;
                diskBsdName = dd->parentDevice;
            }
            break;
        }
      }

      for(std::vector<int>::size_type i = 0; i != deviceList.size(); i++) {
        DeviceDescriptor *dd = &deviceList[i];

        if (dd->device == "/dev/" + diskBsdName) {
          dd->mountpoints.push_back([[path path] UTF8String]);
          dd->mountpointLabels.push_back([volumeName UTF8String]);
          if (!childDevice.empty()) {
              dd->childDevices.push_back("/dev/"+childDevice);
          }
          break;
        }
      }

      CFRelease(disk);
    }
    CFRelease(session);

    return deviceList;
  }

}  // namespace Drivelist
