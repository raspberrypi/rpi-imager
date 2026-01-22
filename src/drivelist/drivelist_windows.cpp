/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Windows drive enumeration using SetupDi and DeviceIoControl.
 *
 * Design notes:
 * - Uses SetupDi API for device enumeration
 * - Uses DeviceIoControl for geometry and adapter info
 * - Handles many edge cases: VHDs, Storage Spaces, RAIDs, Google Drive conflicts
 * - Device number + enumerator matching prevents false mountpoint associations
 */

#include "drivelist.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Target Windows 10+ (consistent with project's PlatformPackaging.cmake)
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <shlobj.h>
#include <devpkey.h>
#include <string>
#include <vector>
#include <set>

// Define if not available in SDK
#ifndef DEVPKEY_Device_EnumeratorName
DEFINE_DEVPROPKEY(DEVPKEY_Device_EnumeratorName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 24);
#endif

namespace Drivelist {

namespace {

// ============================================================================
// Constants
// ============================================================================

const GUID GUID_DEVICE_INTERFACE_DISK = {
    0x53F56307L, 0xB6BF, 0x11D0, {0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B}
};

// USB storage driver names
const std::set<std::string> USB_STORAGE_DRIVERS = {
    "USBSTOR", "UASPSTOR", "VUSBSTOR",
    "RTUSER", "CMIUCR", "EUCR",
    "ETRONSTOR", "ASUSSTPT"
};

// Generic/SCSI storage driver names
const std::set<std::string> GENERIC_STORAGE_DRIVERS = {
    "SCSI", "SD", "PCISTOR",
    "RTSOR", "JMCR", "JMCF", "RIMMPTSK", "RIMSPTSK", "RIXDPTSK",
    "TI21SONY", "ESD7SK", "ESM7SK", "O2MD", "O2SD", "VIACR"
};

// Virtual hard disk hardware ID patterns
const std::set<std::string> VHD_HARDWARE_IDS = {
    "Arsenal_________Virtual_",
    "KernSafeVirtual_________",
    "Msft____Virtual_Disk____",
    "VMware__VMware_Virtual_S"
};

// Windows known folder IDs for system drive detection
const GUID KNOWN_FOLDER_IDS[] = {
    FOLDERID_Windows,
    FOLDERID_Profile,
    FOLDERID_ProgramFiles,
    FOLDERID_ProgramFilesX86
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Convert wide string to UTF-8
 */
std::string wcharToUtf8(const wchar_t* wstr)
{
    if (!wstr) return {};

    int wstrLen = static_cast<int>(wcslen(wstr));
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, wstrLen, nullptr, 0, nullptr, nullptr);
    if (size == 0) return {};

    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, wstrLen, result.data(), size, nullptr, nullptr);
    return result;
}

/**
 * @brief Get device enumerator name (driver name)
 */
std::string getEnumeratorName(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    char buffer[MAX_PATH] = {};
    if (SetupDiGetDeviceRegistryPropertyA(
            hDeviceInfo, &deviceInfoData, SPDRP_ENUMERATOR_NAME,
            nullptr, reinterpret_cast<LPBYTE>(buffer), sizeof(buffer), nullptr)) {
        return buffer;
    }
    return {};
}

/**
 * @brief Get device friendly name
 */
std::string getFriendlyName(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    wchar_t buffer[MAX_PATH] = {};
    if (SetupDiGetDeviceRegistryPropertyW(
            hDeviceInfo, &deviceInfoData, SPDRP_FRIENDLYNAME,
            nullptr, reinterpret_cast<PBYTE>(buffer), sizeof(buffer), nullptr)) {
        return wcharToUtf8(buffer);
    }
    return {};
}

/**
 * @brief Check if device is removable based on removal policy
 */
bool isRemovableDevice(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    DWORD removalPolicy = 0;
    if (SetupDiGetDeviceRegistryProperty(
            hDeviceInfo, &deviceInfoData, SPDRP_REMOVAL_POLICY,
            nullptr, reinterpret_cast<PBYTE>(&removalPolicy), sizeof(removalPolicy), nullptr)) {
        return removalPolicy == CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL ||
               removalPolicy == CM_REMOVAL_POLICY_EXPECT_ORDERLY_REMOVAL;
    }
    return false;
}

/**
 * @brief Check if device is a virtual hard disk
 */
bool isVirtualHardDrive(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    char buffer[MAX_PATH] = {};
    if (!SetupDiGetDeviceRegistryPropertyA(
            hDeviceInfo, &deviceInfoData, SPDRP_HARDWAREID,
            nullptr, reinterpret_cast<LPBYTE>(buffer), sizeof(buffer), nullptr)) {
        return false;
    }

    std::string hardwareId(buffer);
    for (const auto& vhdId : VHD_HARDWARE_IDS) {
        if (hardwareId.find(vhdId) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Get device number from handle
 */
int32_t getDeviceNumber(HANDLE hDevice)
{
    DWORD bytesReturned = 0;

    // Try disk extents first
    VOLUME_DISK_EXTENTS diskExtents = {};
    if (DeviceIoControl(hDevice, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                        nullptr, 0, &diskExtents, sizeof(diskExtents), &bytesReturned, nullptr)) {
        if (diskExtents.NumberOfDiskExtents >= 2) {
            return -1;  // Skip RAIDs
        }
        if (diskExtents.NumberOfDiskExtents > 0) {
            return static_cast<int32_t>(diskExtents.Extents[0].DiskNumber);
        }
    }

    // Try storage device number
    STORAGE_DEVICE_NUMBER deviceNumber = {};
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                        nullptr, 0, &deviceNumber, sizeof(deviceNumber), &bytesReturned, nullptr)) {
        return static_cast<int32_t>(deviceNumber.DeviceNumber);
    }

    return -1;
}

/**
 * @brief Map STORAGE_BUS_TYPE to string
 */
std::string busTypeToString(STORAGE_BUS_TYPE busType)
{
    switch (busType) {
        case BusTypeUnknown: return "UNKNOWN";
        case BusTypeScsi: return "SCSI";
        case BusTypeAtapi: return "ATAPI";
        case BusTypeAta: return "ATA";
        case BusType1394: return "1394";
        case BusTypeSsa: return "SSA";
        case BusTypeFibre: return "FIBRE";
        case BusTypeUsb: return "USB";
        case BusTypeRAID: return "RAID";
        case BusTypeiScsi: return "iSCSI";
        case BusTypeSas: return "SAS";
        case BusTypeSata: return "SATA";
        case BusTypeSd: return "SD";
        case BusTypeMmc: return "MMC";
        case BusTypeVirtual: return "VIRTUAL";
        case BusTypeFileBackedVirtual: return "FILEBACKEDVIRTUAL";
        case BusTypeSpaces: return "SPACES";
        case BusTypeNvme: return "NVME";
        case BusTypeScm: return "SCM";
        case BusTypeUfs: return "UFS";
        default: return "INVALID";
    }
}

/**
 * @brief Get adapter info including bus type
 */
bool getAdapterInfo(HANDLE hPhysical, DeviceDescriptor& device)
{
    STORAGE_PROPERTY_QUERY query = {};
    query.QueryType = PropertyStandardQuery;
    query.PropertyId = StorageAdapterProperty;

    STORAGE_ADAPTER_DESCRIPTOR adapterDesc = {};
    DWORD bytesReturned = 0;

    if (DeviceIoControl(hPhysical, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof(query), &adapterDesc, sizeof(adapterDesc),
                        &bytesReturned, nullptr)) {
        device.busType = busTypeToString(adapterDesc.BusType);
        device.busVersion = std::to_string(adapterDesc.BusMajorVersion) + "." +
                           std::to_string(adapterDesc.BusMinorVersion);
        device.busVersionNull = false;
        return true;
    }
    return false;
}

/**
 * @brief Get device block sizes
 */
bool getBlockSize(HANDLE hPhysical, DeviceDescriptor& device)
{
    STORAGE_PROPERTY_QUERY query = {};
    query.QueryType = PropertyStandardQuery;
    query.PropertyId = StorageAccessAlignmentProperty;

    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR alignDesc = {};
    DWORD bytesReturned = 0;

    if (DeviceIoControl(hPhysical, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof(query), &alignDesc, sizeof(alignDesc),
                        &bytesReturned, nullptr)) {
        device.blockSize = alignDesc.BytesPerPhysicalSector;
        device.logicalBlockSize = alignDesc.BytesPerLogicalSector;
        return true;
    }
    return false;
}

/**
 * @brief Get device size from geometry
 */
bool getDeviceSize(HANDLE hPhysical, DeviceDescriptor& device)
{
    DISK_GEOMETRY_EX geometry = {};
    DWORD bytesReturned = 0;

    if (DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                        nullptr, 0, &geometry, sizeof(geometry), &bytesReturned, nullptr)) {
        device.size = geometry.DiskSize.QuadPart;
        if (device.blockSize == 0) {
            device.blockSize = geometry.Geometry.BytesPerSector;
        }
        return true;
    }
    return false;
}

/**
 * @brief Get enumerator name for a volume (for disambiguation)
 */
std::string getVolumeEnumeratorName(const std::string& volumeName)
{
    // Open the volume to get its device number
    std::string volumePath = "\\\\.\\" + volumeName + ":";
    HANDLE hVolume = CreateFileA(volumePath.c_str(), 0,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hVolume == INVALID_HANDLE_VALUE) return {};

    int32_t targetDeviceNumber = getDeviceNumber(hVolume);
    CloseHandle(hVolume);
    if (targetDeviceNumber == -1) return {};

    // Find the disk device with this number
    HDEVINFO hDeviceInfo = SetupDiGetClassDevsA(
        &GUID_DEVICE_INTERFACE_DISK, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDeviceInfo == INVALID_HANDLE_VALUE) return {};

    std::string result;
    SP_DEVINFO_DATA deviceInfoData = {};
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDeviceInfo, i, &deviceInfoData); i++) {
        SP_DEVICE_INTERFACE_DATA interfaceData = {};
        interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD j = 0; ; j++) {
            if (!SetupDiEnumDeviceInterfaces(hDeviceInfo, &deviceInfoData,
                                              &GUID_DEVICE_INTERFACE_DISK, j, &interfaceData)) {
                break;
            }

            // Get interface detail
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(hDeviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);

            std::vector<BYTE> detailBuffer(requiredSize);
            auto* detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            if (SetupDiGetDeviceInterfaceDetailW(hDeviceInfo, &interfaceData, detailData, requiredSize, nullptr, nullptr)) {
                HANDLE hDevice = CreateFileW(detailData->DevicePath, 0,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hDevice != INVALID_HANDLE_VALUE) {
                    int32_t deviceNum = getDeviceNumber(hDevice);
                    CloseHandle(hDevice);

                    if (deviceNum == targetDeviceNumber) {
                        result = getEnumeratorName(hDeviceInfo, deviceInfoData);
                        break;
                    }
                }
            }
        }
        if (!result.empty()) break;
    }

    SetupDiDestroyDeviceInfoList(hDeviceInfo);
    return result;
}

/**
 * @brief Get all available drive letters
 */
std::vector<std::string> getAvailableVolumes()
{
    std::vector<std::string> volumes;
    DWORD mask = GetLogicalDrives();

    for (char letter = 'A'; mask; letter++, mask >>= 1) {
        if (mask & 1) {
            volumes.push_back(std::string(1, letter));
        }
    }
    return volumes;
}

/**
 * @brief Get mountpoints for a device, with enumerator disambiguation
 */
void getMountpoints(int32_t deviceNumber, const std::string& deviceEnumerator,
                    std::vector<std::string>& mountpoints)
{
    for (const auto& volumeName : getAvailableVolumes()) {
        std::string volumePath = "\\\\.\\" + volumeName + ":";
        std::string drivePath = volumeName + ":\\";

        // Only check fixed and removable drives
        UINT driveType = GetDriveTypeA(drivePath.c_str());
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
            continue;
        }

        HANDLE hVolume = CreateFileA(volumePath.c_str(), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hVolume == INVALID_HANDLE_VALUE) continue;

        int32_t volumeDeviceNumber = getDeviceNumber(hVolume);
        CloseHandle(hVolume);

        if (volumeDeviceNumber == deviceNumber) {
            // Extra disambiguation: check enumerator to avoid Google Drive conflicts
            std::string volumeEnumerator = getVolumeEnumeratorName(volumeName);
            if (!deviceEnumerator.empty() && !volumeEnumerator.empty() &&
                volumeEnumerator != deviceEnumerator) {
                continue;  // Different drivers, skip
            }
            mountpoints.push_back(drivePath);
        }
    }
}

/**
 * @brief Check if any mountpoint contains system folders
 */
bool isSystemDevice(const std::vector<std::string>& mountpoints)
{
    for (const GUID& folderId : KNOWN_FOLDER_IDS) {
        PWSTR folderPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &folderPath))) {
            std::string systemPath = wcharToUtf8(folderPath);
            CoTaskMemFree(folderPath);

            for (const auto& mountpoint : mountpoints) {
                if (systemPath.find(mountpoint) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

/**
 * @brief Get detailed device information
 */
bool getDetailData(DeviceDescriptor& device, HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    SP_DEVICE_INTERFACE_DATA interfaceData = {};
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(hDeviceInfo, &deviceInfoData,
                                      &GUID_DEVICE_INTERFACE_DISK, 0, &interfaceData)) {
        return false;
    }

    // Get interface detail
    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(hDeviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);

    std::vector<BYTE> detailBuffer(requiredSize);
    auto* detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(hDeviceInfo, &interfaceData, detailData, requiredSize, nullptr, nullptr)) {
        device.error = "Failed to get device interface detail";
        return false;
    }

    // Open device handle (no permissions needed for metadata)
    HANDLE hDevice = CreateFileW(detailData->DevicePath, 0,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hDevice == INVALID_HANDLE_VALUE) {
        device.error = "Failed to open device handle";
        return false;
    }

    int32_t deviceNumber = getDeviceNumber(hDevice);
    CloseHandle(hDevice);

    if (deviceNumber == -1) {
        device.error = "Failed to get device number";
        return false;
    }

    device.device = "\\\\.\\PhysicalDrive" + std::to_string(deviceNumber);
    device.raw = device.device;

    // Get mountpoints
    getMountpoints(deviceNumber, device.enumerator, device.mountpoints);

    // Open physical drive for geometry/adapter info
    HANDLE hPhysical = CreateFileA(device.device.c_str(), 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hPhysical == INVALID_HANDLE_VALUE) {
        device.error = "Failed to open physical device";
        return false;
    }

    bool success = getDeviceSize(hPhysical, device);
    if (success) {
        getAdapterInfo(hPhysical, device);
        getBlockSize(hPhysical, device);

        // Check if writable
        DWORD bytesReturned = 0;
        device.isReadOnly = !DeviceIoControl(hPhysical, IOCTL_DISK_IS_WRITABLE,
                                              nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
    } else {
        device.error = "Failed to get device geometry";
    }

    CloseHandle(hPhysical);
    return success;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

std::vector<DeviceDescriptor> ListStorageDevices()
{
    std::vector<DeviceDescriptor> deviceList;

    HDEVINFO hDeviceInfo = SetupDiGetClassDevsA(
        &GUID_DEVICE_INTERFACE_DISK, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (hDeviceInfo == INVALID_HANDLE_VALUE) {
        return deviceList;
    }

    SP_DEVINFO_DATA deviceInfoData = {};
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDeviceInfo, i, &deviceInfoData); i++) {
        DeviceDescriptor device;

        // Get basic device info from registry
        device.enumerator = getEnumeratorName(hDeviceInfo, deviceInfoData);
        device.description = getFriendlyName(hDeviceInfo, deviceInfoData);
        device.isRemovable = isRemovableDevice(hDeviceInfo, deviceInfoData);
        device.isVirtual = isVirtualHardDrive(hDeviceInfo, deviceInfoData);

        // Classify by driver name
        device.isSCSI = GENERIC_STORAGE_DRIVERS.count(device.enumerator) > 0;
        device.isUSB = USB_STORAGE_DRIVERS.count(device.enumerator) > 0;
        device.isCard = (device.enumerator == "SD");

        // Initial system drive guess (refined later with mountpoint info)
        device.isSystem = !device.isRemovable &&
                          (device.enumerator == "SCSI" || device.enumerator == "IDE");

        device.devicePathNull = true;

        // Get detailed hardware info
        if (getDetailData(device, hDeviceInfo, deviceInfoData)) {
            // Refine classification based on bus type
            device.isCard = (device.busType == "SD" || device.busType == "MMC" || device.busType == "UFS");

            // SD/MMC/UFS cards and USB devices should never be system drives
            if (!device.isCard && !device.isUSB) {
                device.isSystem = device.isSystem || isSystemDevice(device.mountpoints);
            } else {
                device.isSystem = false;
            }

            // Detect UAS (USB Attached SCSI)
            device.isUAS = (device.enumerator == "SCSI" && device.busType == "USB");
            device.isUASNull = false;

            // Detect virtual devices by bus type
            device.isVirtual = device.isVirtual ||
                               device.busType == "VIRTUAL" ||
                               device.busType == "FILEBACKEDVIRTUAL" ||
                               device.busType == "SPACES";

            // SCM and NVMeoF are always system drives
            if (device.busType == "SCM" || device.busType == "NVMEOF") {
                device.isSystem = true;
            }
        }

        deviceList.push_back(std::move(device));
    }

    SetupDiDestroyDeviceInfoList(hDeviceInfo);

    return deviceList;
}

// ============================================================================
// Test API
// ============================================================================

#ifdef DRIVELIST_ENABLE_TEST_API

namespace testing {

std::string windowsBusTypeToString(int busType)
{
    return busTypeToString(static_cast<STORAGE_BUS_TYPE>(busType));
}

bool isWindowsSystemDevice(const std::vector<std::string>& mountpoints)
{
    return isSystemDevice(mountpoints);
}

} // namespace testing

#endif // DRIVELIST_ENABLE_TEST_API

} // namespace Drivelist
