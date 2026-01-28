/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Windows drive enumeration using SetupDi and DeviceIoControl.
 *
 * Design notes:
 * - Uses SetupDi API for device enumeration (Unicode throughout)
 * - Uses DeviceIoControl for geometry and adapter info
 * - Handles many edge cases: VHDs, Storage Spaces, RAIDs, Google Drive conflicts
 * - Device number + enumerator matching prevents false mountpoint associations
 * - RAII wrappers for all Windows handles to prevent resource leaks
 */

#include "drivelist.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Target Windows 10+ (consistent with project's PlatformPackaging.cmake)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <shlobj.h>
#include <devpkey.h>
#include <objbase.h>  // CoInitializeEx
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <algorithm>
#include <cctype>

// Link required libraries
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// Define if not available in SDK
#ifndef DEVPKEY_Device_EnumeratorName
DEFINE_DEVPROPKEY(DEVPKEY_Device_EnumeratorName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 24);
#endif

namespace Drivelist {

namespace {

// ============================================================================
// Debug Tracing
// ============================================================================

// Compile-time flag for verbose debug output
#ifdef DRIVELIST_DEBUG
#define DRIVELIST_TRACE(msg) OutputDebugStringA("Drivelist: " msg "\n")
#define DRIVELIST_TRACEW(msg) OutputDebugStringW(L"Drivelist: " msg L"\n")
#define DRIVELIST_TRACE_ERROR(api) do { \
    char _buf[256]; \
    DWORD _err = GetLastError(); \
    snprintf(_buf, sizeof(_buf), "Drivelist: %s failed with error %lu (0x%08lX)\n", api, _err, _err); \
    OutputDebugStringA(_buf); \
} while(0)
#else
#define DRIVELIST_TRACE(msg) ((void)0)
#define DRIVELIST_TRACEW(msg) ((void)0)
#define DRIVELIST_TRACE_ERROR(api) ((void)0)
#endif

// ============================================================================
// RAII Wrappers for Windows Handles
// ============================================================================

// Custom deleter for HANDLE (CloseHandle)
struct HandleDeleter {
    void operator()(HANDLE h) const noexcept {
        if (h && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};

// Unique pointer type for HANDLE
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleDeleter>;

// Factory function to create UniqueHandle (handles INVALID_HANDLE_VALUE)
inline UniqueHandle makeUniqueHandle(HANDLE h) noexcept {
    return UniqueHandle((h != INVALID_HANDLE_VALUE) ? h : nullptr);
}

// Custom deleter for HDEVINFO (SetupDiDestroyDeviceInfoList)
struct DevInfoDeleter {
    void operator()(HDEVINFO h) const noexcept {
        if (h && h != INVALID_HANDLE_VALUE) {
            SetupDiDestroyDeviceInfoList(h);
        }
    }
};

// Unique pointer type for HDEVINFO
using UniqueDevInfo = std::unique_ptr<std::remove_pointer_t<HDEVINFO>, DevInfoDeleter>;

// Factory function to create UniqueDevInfo
inline UniqueDevInfo makeUniqueDevInfo(HDEVINFO h) noexcept {
    return UniqueDevInfo((h != INVALID_HANDLE_VALUE) ? h : nullptr);
}

// RAII wrapper for COM initialization (per-thread)
class ComInitializer {
public:
    ComInitializer() noexcept {
        // COINIT_APARTMENTTHREADED is safest for GUI apps
        // Don't fail if already initialized (RPC_E_CHANGED_MODE)
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        m_initialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        m_needsUninit = SUCCEEDED(hr);
    }
    
    ~ComInitializer() noexcept {
        if (m_needsUninit) {
            CoUninitialize();
        }
    }
    
    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;
    
    explicit operator bool() const noexcept { return m_initialized; }
    
private:
    bool m_initialized = false;
    bool m_needsUninit = false;
};

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
 * 
 * Uses WideCharToMultiByte with proper error handling.
 * Returns empty string on failure (never throws).
 */
std::string wcharToUtf8(const wchar_t* wstr)
{
    if (!wstr || !*wstr) return {};

    // Get required buffer size
    int wstrLen = static_cast<int>(wcslen(wstr));
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, wstrLen, nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        DRIVELIST_TRACE_ERROR("WideCharToMultiByte (size query)");
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    int written = WideCharToMultiByte(CP_UTF8, 0, wstr, wstrLen, result.data(), size, nullptr, nullptr);
    if (written == 0) {
        DRIVELIST_TRACE_ERROR("WideCharToMultiByte (convert)");
        return {};
    }

    return result;
}

/**
 * @brief Case-insensitive string comparison for driver names
 */
bool equalsIgnoreCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char ca, char cb) {
        return std::toupper(static_cast<unsigned char>(ca)) == 
               std::toupper(static_cast<unsigned char>(cb));
    });
}

/**
 * @brief Case-insensitive set membership check
 */
bool containsIgnoreCase(const std::set<std::string>& s, const std::string& value)
{
    for (const auto& item : s) {
        if (equalsIgnoreCase(item, value)) return true;
    }
    return false;
}

/**
 * @brief Get device registry property with dynamic buffer sizing
 * 
 * Properly handles variable-length properties by querying size first.
 * Uses Unicode API throughout for consistency.
 */
std::wstring getDeviceRegistryPropertyW(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData, DWORD property)
{
    // Query required size
    DWORD requiredSize = 0;
    DWORD dataType = 0;
    SetupDiGetDeviceRegistryPropertyW(hDeviceInfo, &deviceInfoData, property,
                                       &dataType, nullptr, 0, &requiredSize);
    
    DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0) {
        // Property doesn't exist or other error - not necessarily a problem
        return {};
    }
    
    // Allocate buffer and retrieve
    std::vector<BYTE> buffer(requiredSize);
    if (!SetupDiGetDeviceRegistryPropertyW(hDeviceInfo, &deviceInfoData, property,
                                            &dataType, buffer.data(), requiredSize, nullptr)) {
        DRIVELIST_TRACE_ERROR("SetupDiGetDeviceRegistryPropertyW");
        return {};
    }
    
    // REG_SZ or first string of REG_MULTI_SZ
    if (dataType == REG_SZ || dataType == REG_MULTI_SZ) {
        return std::wstring(reinterpret_cast<wchar_t*>(buffer.data()));
    }
    
    return {};
}

/**
 * @brief Get all strings from a REG_MULTI_SZ property (e.g., Hardware IDs)
 */
std::vector<std::wstring> getDeviceRegistryMultiSzW(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData, DWORD property)
{
    std::vector<std::wstring> result;
    
    // Query required size
    DWORD requiredSize = 0;
    DWORD dataType = 0;
    SetupDiGetDeviceRegistryPropertyW(hDeviceInfo, &deviceInfoData, property,
                                       &dataType, nullptr, 0, &requiredSize);
    
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0) {
        return result;
    }
    
    // Allocate and retrieve
    std::vector<BYTE> buffer(requiredSize);
    if (!SetupDiGetDeviceRegistryPropertyW(hDeviceInfo, &deviceInfoData, property,
                                            &dataType, buffer.data(), requiredSize, nullptr)) {
        return result;
    }
    
    if (dataType != REG_MULTI_SZ && dataType != REG_SZ) {
        return result;
    }
    
    // Parse double-null-terminated string list
    const wchar_t* p = reinterpret_cast<wchar_t*>(buffer.data());
    const wchar_t* end = reinterpret_cast<wchar_t*>(buffer.data() + buffer.size());
    
    while (p < end && *p) {
        result.emplace_back(p);
        p += wcslen(p) + 1;
    }
    
    return result;
}

/**
 * @brief Get device enumerator name (driver name)
 */
std::string getEnumeratorName(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    std::wstring nameW = getDeviceRegistryPropertyW(hDeviceInfo, deviceInfoData, SPDRP_ENUMERATOR_NAME);
    return wcharToUtf8(nameW.c_str());
}

/**
 * @brief Get device friendly name
 */
std::string getFriendlyName(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    std::wstring nameW = getDeviceRegistryPropertyW(hDeviceInfo, deviceInfoData, SPDRP_FRIENDLYNAME);
    return wcharToUtf8(nameW.c_str());
}

/**
 * @brief Check if device is removable based on removal policy
 */
bool isRemovableDevice(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    DWORD removalPolicy = 0;
    DWORD dataType = 0;
    DWORD requiredSize = 0;
    
    if (SetupDiGetDeviceRegistryPropertyW(
            hDeviceInfo, &deviceInfoData, SPDRP_REMOVAL_POLICY,
            &dataType, reinterpret_cast<PBYTE>(&removalPolicy), sizeof(removalPolicy), &requiredSize)) {
        return removalPolicy == CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL ||
               removalPolicy == CM_REMOVAL_POLICY_EXPECT_ORDERLY_REMOVAL;
    }
    DRIVELIST_TRACE_ERROR("SetupDiGetDeviceRegistryPropertyW (REMOVAL_POLICY)");
    return false;
}

/**
 * @brief Check if device is a virtual hard disk
 * 
 * Properly handles SPDRP_HARDWAREID as REG_MULTI_SZ - checks ALL hardware IDs,
 * not just the first one. This is important because compatible IDs and
 * instance IDs can identify VHDs that the primary ID doesn't.
 */
bool isVirtualHardDrive(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    // Get all hardware IDs (REG_MULTI_SZ)
    auto hardwareIds = getDeviceRegistryMultiSzW(hDeviceInfo, deviceInfoData, SPDRP_HARDWAREID);
    
    for (const auto& hwId : hardwareIds) {
        std::string hwIdUtf8 = wcharToUtf8(hwId.c_str());
        for (const auto& vhdId : VHD_HARDWARE_IDS) {
            if (hwIdUtf8.find(vhdId) != std::string::npos) {
                DRIVELIST_TRACE("Detected VHD by hardware ID");
                return true;
            }
        }
    }
    
    // Also check compatible IDs (secondary identification)
    auto compatIds = getDeviceRegistryMultiSzW(hDeviceInfo, deviceInfoData, SPDRP_COMPATIBLEIDS);
    
    for (const auto& compatId : compatIds) {
        std::string compatIdUtf8 = wcharToUtf8(compatId.c_str());
        for (const auto& vhdId : VHD_HARDWARE_IDS) {
            if (compatIdUtf8.find(vhdId) != std::string::npos) {
                DRIVELIST_TRACE("Detected VHD by compatible ID");
                return true;
            }
        }
    }
    
    return false;
}

/**
 * @brief Get device number from handle
 * 
 * Tries multiple methods in order of reliability:
 * 1. IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS (volumes/partitions)
 * 2. IOCTL_STORAGE_GET_DEVICE_NUMBER (physical disks)
 * 
 * Returns -1 for RAIDs/spanned volumes (multiple extents) or on failure.
 */
int32_t getDeviceNumber(HANDLE hDevice)
{
    if (!hDevice || hDevice == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    DWORD bytesReturned = 0;

    // Try disk extents first (works for volumes)
    VOLUME_DISK_EXTENTS diskExtents = {};
    if (DeviceIoControl(hDevice, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                        nullptr, 0, &diskExtents, sizeof(diskExtents), &bytesReturned, nullptr)) {
        if (diskExtents.NumberOfDiskExtents >= 2) {
            DRIVELIST_TRACE("Skipping multi-extent volume (RAID/spanned)");
            return -1;  // Skip RAIDs/spanned volumes
        }
        if (diskExtents.NumberOfDiskExtents > 0) {
            return static_cast<int32_t>(diskExtents.Extents[0].DiskNumber);
        }
    }

    // Try storage device number (works for physical disks)
    STORAGE_DEVICE_NUMBER deviceNumber = {};
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                        nullptr, 0, &deviceNumber, sizeof(deviceNumber), &bytesReturned, nullptr)) {
        return static_cast<int32_t>(deviceNumber.DeviceNumber);
    }

    DRIVELIST_TRACE_ERROR("DeviceIoControl (get device number)");
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
 * 
 * This is used to disambiguate volumes that share the same device number
 * but come from different drivers (e.g., Google Drive FS vs physical disk).
 */
std::string getVolumeEnumeratorName(const std::string& volumeName)
{
    // Open the volume to get its device number (using wide string for Unicode support)
    std::wstring volumePathW = L"\\\\.\\" + std::wstring(volumeName.begin(), volumeName.end()) + L":";
    UniqueHandle hVolume = makeUniqueHandle(CreateFileW(volumePathW.c_str(), 0,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!hVolume) {
        DRIVELIST_TRACE_ERROR("CreateFileW (volume)");
        return {};
    }

    int32_t targetDeviceNumber = getDeviceNumber(hVolume.get());
    if (targetDeviceNumber == -1) return {};

    // Find the disk device with this number
    UniqueDevInfo hDeviceInfo = makeUniqueDevInfo(SetupDiGetClassDevsW(
        &GUID_DEVICE_INTERFACE_DISK, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (!hDeviceInfo) {
        DRIVELIST_TRACE_ERROR("SetupDiGetClassDevsW");
        return {};
    }

    std::string result;
    SP_DEVINFO_DATA deviceInfoData = {};
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDeviceInfo.get(), i, &deviceInfoData); i++) {
        SP_DEVICE_INTERFACE_DATA interfaceData = {};
        interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD j = 0; ; j++) {
            if (!SetupDiEnumDeviceInterfaces(hDeviceInfo.get(), &deviceInfoData,
                                              &GUID_DEVICE_INTERFACE_DISK, j, &interfaceData)) {
                break;
            }

            // Get interface detail - first call gets required size
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(hDeviceInfo.get(), &interfaceData, nullptr, 0, &requiredSize, nullptr);
            
            // Verify we got ERROR_INSUFFICIENT_BUFFER (expected) not some other error
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0) {
                continue;
            }

            std::vector<BYTE> detailBuffer(requiredSize);
            auto* detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            if (SetupDiGetDeviceInterfaceDetailW(hDeviceInfo.get(), &interfaceData, detailData, requiredSize, nullptr, nullptr)) {
                UniqueHandle hDevice = makeUniqueHandle(CreateFileW(detailData->DevicePath, 0,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
                if (hDevice) {
                    int32_t deviceNum = getDeviceNumber(hDevice.get());
                    if (deviceNum == targetDeviceNumber) {
                        result = getEnumeratorName(hDeviceInfo.get(), deviceInfoData);
                        break;
                    }
                }
            }
        }
        if (!result.empty()) break;
    }

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
 * 
 * Uses enumerator comparison to handle cases where different drivers
 * assign the same device number (e.g., Google Drive FS virtual drive).
 */
void getMountpoints(int32_t deviceNumber, const std::string& deviceEnumerator,
                    std::vector<std::string>& mountpoints)
{
    for (const auto& volumeName : getAvailableVolumes()) {
        std::wstring volumePathW = L"\\\\.\\" + std::wstring(volumeName.begin(), volumeName.end()) + L":";
        std::wstring drivePathW = std::wstring(volumeName.begin(), volumeName.end()) + L":\\";
        std::string drivePath = volumeName + ":\\";

        // Only check fixed and removable drives
        UINT driveType = GetDriveTypeW(drivePathW.c_str());
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
            continue;
        }

        UniqueHandle hVolume = makeUniqueHandle(CreateFileW(volumePathW.c_str(), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!hVolume) continue;

        int32_t volumeDeviceNumber = getDeviceNumber(hVolume.get());

        if (volumeDeviceNumber == deviceNumber) {
            // Extra disambiguation: check enumerator to avoid Google Drive conflicts
            // Use case-insensitive comparison for driver names
            std::string volumeEnumerator = getVolumeEnumeratorName(volumeName);
            if (!deviceEnumerator.empty() && !volumeEnumerator.empty() &&
                !equalsIgnoreCase(volumeEnumerator, deviceEnumerator)) {
                DRIVELIST_TRACE("Skipping volume due to enumerator mismatch");
                continue;  // Different drivers, skip
            }
            mountpoints.push_back(drivePath);
        }
    }
}

/**
 * @brief Check if any mountpoint contains system folders
 * 
 * Requires COM initialization for SHGetKnownFolderPath. Uses RAII wrapper
 * to ensure proper cleanup even on early return.
 * 
 * Note: Case-insensitive path comparison for Windows paths.
 */
bool isSystemDevice(const std::vector<std::string>& mountpoints)
{
    // Ensure COM is initialized for shell functions
    ComInitializer comInit;
    if (!comInit) {
        DRIVELIST_TRACE("Warning: COM initialization failed, assuming non-system device");
        return false;
    }
    
    for (const GUID& folderId : KNOWN_FOLDER_IDS) {
        PWSTR folderPath = nullptr;
        HRESULT hr = SHGetKnownFolderPath(folderId, 0, nullptr, &folderPath);
        if (SUCCEEDED(hr) && folderPath) {
            std::string systemPath = wcharToUtf8(folderPath);
            CoTaskMemFree(folderPath);

            for (const auto& mountpoint : mountpoints) {
                // Case-insensitive prefix match for Windows paths
                if (systemPath.size() >= mountpoint.size()) {
                    bool match = std::equal(mountpoint.begin(), mountpoint.end(), systemPath.begin(),
                                           [](char a, char b) {
                                               return std::toupper(static_cast<unsigned char>(a)) ==
                                                      std::toupper(static_cast<unsigned char>(b));
                                           });
                    if (match) {
                        DRIVELIST_TRACE("Detected system device by known folder path");
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/**
 * @brief Get detailed device information using DeviceIoControl
 * 
 * Opens the device interface to query device number, then opens the
 * physical drive to get geometry, adapter info, and block sizes.
 * Uses RAII for all handles to ensure cleanup on all code paths.
 */
bool getDetailData(DeviceDescriptor& device, HDEVINFO hDeviceInfo, SP_DEVINFO_DATA& deviceInfoData)
{
    SP_DEVICE_INTERFACE_DATA interfaceData = {};
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(hDeviceInfo, &deviceInfoData,
                                      &GUID_DEVICE_INTERFACE_DISK, 0, &interfaceData)) {
        DRIVELIST_TRACE_ERROR("SetupDiEnumDeviceInterfaces");
        return false;
    }

    // Get interface detail - first call gets required size (expected to fail)
    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(hDeviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
    
    // Verify we got the expected error
    DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER || requiredSize == 0) {
        DRIVELIST_TRACE_ERROR("SetupDiGetDeviceInterfaceDetailW (size query)");
        device.error = "Failed to query interface detail size";
        return false;
    }

    std::vector<BYTE> detailBuffer(requiredSize);
    auto* detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(hDeviceInfo, &interfaceData, detailData, requiredSize, nullptr, nullptr)) {
        DRIVELIST_TRACE_ERROR("SetupDiGetDeviceInterfaceDetailW");
        device.error = "Failed to get device interface detail";
        return false;
    }

    // Store device path for debugging (useful for support cases)
    device.devicePath = wcharToUtf8(detailData->DevicePath);
    device.devicePathNull = device.devicePath.empty();

    // Open device interface handle (no permissions needed for metadata queries)
    UniqueHandle hDevice = makeUniqueHandle(CreateFileW(detailData->DevicePath, 0,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!hDevice) {
        DRIVELIST_TRACE_ERROR("CreateFileW (device interface)");
        device.error = "Failed to open device handle (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    int32_t deviceNumber = getDeviceNumber(hDevice.get());
    if (deviceNumber == -1) {
        device.error = "Failed to get device number";
        return false;
    }

    device.device = "\\\\.\\PhysicalDrive" + std::to_string(deviceNumber);
    device.raw = device.device;

    // Get mountpoints (can release interface handle now)
    hDevice.reset();
    getMountpoints(deviceNumber, device.enumerator, device.mountpoints);

    // Open physical drive for geometry/adapter info
    std::wstring physicalDriveW = L"\\\\.\\PhysicalDrive" + std::to_wstring(deviceNumber);
    UniqueHandle hPhysical = makeUniqueHandle(CreateFileW(physicalDriveW.c_str(), 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!hPhysical) {
        DRIVELIST_TRACE_ERROR("CreateFileW (physical drive)");
        device.error = "Failed to open physical device (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    bool success = getDeviceSize(hPhysical.get(), device);
    if (success) {
        getAdapterInfo(hPhysical.get(), device);
        getBlockSize(hPhysical.get(), device);

        // Check if writable (IOCTL_DISK_IS_WRITABLE succeeds if writable)
        DWORD bytesReturned = 0;
        device.isReadOnly = !DeviceIoControl(hPhysical.get(), IOCTL_DISK_IS_WRITABLE,
                                              nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
    } else {
        DRIVELIST_TRACE_ERROR("DeviceIoControl (get geometry)");
        device.error = "Failed to get device geometry";
    }

    return success;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

std::vector<DeviceDescriptor> ListStorageDevices()
{
    DRIVELIST_TRACE("ListStorageDevices starting");
    
    std::vector<DeviceDescriptor> deviceList;

    // Use Unicode API throughout for consistency
    UniqueDevInfo hDeviceInfo = makeUniqueDevInfo(SetupDiGetClassDevsW(
        &GUID_DEVICE_INTERFACE_DISK, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

    if (!hDeviceInfo) {
        // Return a sentinel device with error message so UI can display failure
        // instead of showing an empty list (which looks like "no drives found")
        DWORD error = GetLastError();
        DRIVELIST_TRACE_ERROR("SetupDiGetClassDevsW");
        
        DeviceDescriptor errorDevice;
        errorDevice.device = "__error__";
        errorDevice.error = "Failed to enumerate drives: SetupDiGetClassDevs failed (error " + std::to_string(error) + ")";
        errorDevice.description = "Drive enumeration failed";
        deviceList.push_back(std::move(errorDevice));
        return deviceList;
    }

    SP_DEVINFO_DATA deviceInfoData = {};
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    // Count devices for reserve (optional optimization)
    DWORD deviceCount = 0;
    while (SetupDiEnumDeviceInfo(hDeviceInfo.get(), deviceCount, &deviceInfoData)) {
        ++deviceCount;
    }
    deviceList.reserve(static_cast<size_t>(deviceCount));
    
    // Reset for actual enumeration
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDeviceInfo.get(), i, &deviceInfoData); i++) {
        DeviceDescriptor device;

        // Get basic device info from registry
        // Sanitize description to prevent Unicode display attacks
        device.enumerator = getEnumeratorName(hDeviceInfo.get(), deviceInfoData);
        device.description = sanitizeForDisplay(getFriendlyName(hDeviceInfo.get(), deviceInfoData));
        device.isRemovable = isRemovableDevice(hDeviceInfo.get(), deviceInfoData);
        device.isVirtual = isVirtualHardDrive(hDeviceInfo.get(), deviceInfoData);

        // Classify by driver name (case-insensitive comparison for driver names)
        device.isSCSI = containsIgnoreCase(GENERIC_STORAGE_DRIVERS, device.enumerator);
        device.isUSB = containsIgnoreCase(USB_STORAGE_DRIVERS, device.enumerator);
        device.isCard = equalsIgnoreCase(device.enumerator, "SD");

        // Initial system drive guess (refined later with mountpoint info)
        device.isSystem = !device.isRemovable &&
                          (equalsIgnoreCase(device.enumerator, "SCSI") || 
                           equalsIgnoreCase(device.enumerator, "IDE"));

        device.devicePathNull = true;

        // Get detailed hardware info
        if (getDetailData(device, hDeviceInfo.get(), deviceInfoData)) {
            // Refine classification based on bus type (these are our constants, case-sensitive OK)
            device.isCard = (device.busType == "SD" || device.busType == "MMC" || device.busType == "UFS");

            // SD/MMC/UFS cards and USB devices should never be system drives
            if (!device.isCard && !device.isUSB) {
                device.isSystem = device.isSystem || isSystemDevice(device.mountpoints);
            } else {
                device.isSystem = false;
            }

            // Detect UAS (USB Attached SCSI)
            device.isUAS = (equalsIgnoreCase(device.enumerator, "SCSI") && device.busType == "USB");
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

    DRIVELIST_TRACE("ListStorageDevices complete");
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
