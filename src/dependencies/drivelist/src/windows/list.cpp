/*
 * Copyright 2017 balena.io
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

// See https://support.microsoft.com/en-us/kb/165721
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define _WIN32_WINNT   0x0601

#include <windows.h>
#include <winioctl.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <shlobj.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <nan.h>
#include <wchar.h>
#include <string>
#include <vector>
#include <set>
#include "../drivelist.hpp"
#include "list.hpp"

#include <devpkey.h>

// Maxnet edit
#ifndef DEVPKEY_Device_EnumeratorName
DEFINE_DEVPROPKEY(DEVPKEY_Device_EnumeratorName, 0xa45c254e,0xdf1c,0x4efd,0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0, 24);
#endif
//

namespace Drivelist {

std::string WCharToUtf8String(const wchar_t* wstr) {
  if (!wstr) {
    return std::string();
  }

  int wstrSize = (int) wcslen(wstr);
  int size = WideCharToMultiByte(
    CP_UTF8, 0, wstr, wstrSize, NULL, 0, NULL, NULL);
  if (!size) {
    return std::string();
  }

  std::string result(size, 0);
  int utf8Size = WideCharToMultiByte(
    CP_UTF8, 0, wstr, wstrSize, &result[0], size, NULL, NULL);
  if (utf8Size != size) {
    return std::string();
  }

  return result;
}

std::string GetEnumeratorName(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA deviceInfoData) {
  char buffer[MAX_PATH];

  ZeroMemory(&buffer, sizeof(buffer));

  BOOL hasEnumeratorName = SetupDiGetDeviceRegistryPropertyA(
    hDeviceInfo, &deviceInfoData, SPDRP_ENUMERATOR_NAME,
    NULL, (LPBYTE) buffer, sizeof(buffer), NULL);

  /*if (!hasEnumeratorName)
  {
      hasEnumeratorName = SetupDiGetDevicePropertyW(hDeviceInfo, &deviceInfoData, &DEVPKEY_Device_EnumeratorName, NULL, (LPBYTE) buffer, sizeof(buffer), NULL, 0);
  }*/

  return hasEnumeratorName ? std::string(buffer) : std::string();
}

std::string GetFriendlyName(HDEVINFO hDeviceInfo,
  SP_DEVINFO_DATA deviceInfoData) {
  wchar_t wbuffer[MAX_PATH];

  ZeroMemory(&wbuffer, sizeof(wbuffer));

  BOOL hasFriendlyName = SetupDiGetDeviceRegistryPropertyW(
    hDeviceInfo, &deviceInfoData, SPDRP_FRIENDLYNAME,
    NULL, (PBYTE) wbuffer, sizeof(wbuffer), NULL);

  return hasFriendlyName ? WCharToUtf8String(wbuffer) : std::string("");
}

bool IsSCSIDevice(std::string enumeratorName) {
  for (std::string driverName : GENERIC_STORAGE_DRIVERS) {
    if (enumeratorName == driverName) {
      return true;
    }
  }

  return false;
}

bool IsUSBDevice(std::string enumeratorName) {
  for (std::string driverName : USB_STORAGE_DRIVERS) {
    if (enumeratorName == driverName) {
      return true;
    }
  }

  return false;
}

bool IsRemovableDevice(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA deviceInfoData) {
  DWORD result = 0;

  BOOL hasRemovalPolicy = SetupDiGetDeviceRegistryProperty(
    hDeviceInfo, &deviceInfoData, SPDRP_REMOVAL_POLICY,
    NULL, (PBYTE) &result, sizeof(result), NULL);

  switch (result) {
    case CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL:
    case CM_REMOVAL_POLICY_EXPECT_ORDERLY_REMOVAL:
      return true;
    default:
      return false;
  }

  return false;
}

bool IsVirtualHardDrive(HDEVINFO hDeviceInfo, SP_DEVINFO_DATA deviceInfoData) {
  char buffer[MAX_PATH];

  ZeroMemory(&buffer, sizeof(buffer));

  BOOL hasHardwareId = SetupDiGetDeviceRegistryPropertyA(
    hDeviceInfo, &deviceInfoData, SPDRP_HARDWAREID,
    NULL, (LPBYTE) buffer, sizeof(buffer), NULL);

  if (!hasHardwareId) {
    return false;
  }

  // printf("SPDRP_HARDWAREID: %s\n", buffer);

  std::string hardwareId(buffer);

  for (std::string vhdHardwareId : VHD_HARDWARE_IDS) {
    if (hardwareId.find(vhdHardwareId, 0) != std::string::npos) {
      return true;
    }
  }

  return false;
}

bool IsSystemDevice(HDEVINFO hDeviceInfo, DeviceDescriptor *device) {
  PWSTR folderPath = NULL;
  BOOL isSystem = false;

  for (const GUID folderId : KNOWN_FOLDER_IDS) {
    HRESULT result = SHGetKnownFolderPath(
      folderId, 0, NULL, &folderPath);

    if (result == S_OK) {
      std::string systemPath = WCharToUtf8String(folderPath);
      // printf("systemPath %s\n", systemPath.c_str());
      for (std::string mountpoint : device->mountpoints) {
        // printf("%s find %s\n", systemPath.c_str(), mountpoint.c_str());
        if (systemPath.find(mountpoint, 0) != std::string::npos) {
          isSystem = true;
          break;
        }
      }
    }

    CoTaskMemFree(folderPath);
  }

  return isSystem;
}

std::vector<std::string> GetAvailableVolumes() {
  DWORD logicalDrivesMask = GetLogicalDrives();
  std::vector<std::string> logicalDrives;

  if (logicalDrivesMask == 0) {
    return logicalDrives;
  }

  char currentDriveLetter = 'A';

  while (logicalDrivesMask) {
    if (logicalDrivesMask & 1) {
      logicalDrives.push_back(std::string(1, currentDriveLetter));
    }
    currentDriveLetter++;
    logicalDrivesMask >>= 1;
  }

  return logicalDrives;
}

int32_t GetDeviceNumber(HANDLE hDevice) {
  BOOL result;
  DWORD size;
  DWORD errorCode = 0;
  int32_t diskNumber = -1;

  STORAGE_DEVICE_NUMBER deviceNumber;
  VOLUME_DISK_EXTENTS diskExtents;

  // Some devices will have the diskNumber exposed through their disk extents,
  // while most of them will only have one accessible through
  // `IOCTL_STORAGE_GET_DEVICE_NUMBER`, so we check this one first,
  // augmenting / overriding it with the latter
  result = DeviceIoControl(
    hDevice, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
    &diskExtents, sizeof(VOLUME_DISK_EXTENTS), &size, NULL);

  if (result && diskExtents.NumberOfDiskExtents > 0) {
    // printf("[INFO] DiskNumber: %i\n", diskExtents.Extents[0].DiskNumber);

    // NOTE: Always ignore RAIDs
    // TODO(jhermsmeier): Handle RAIDs properly
    if (diskExtents.NumberOfDiskExtents >= 2) {
      // printf("[INFO] Possible RAID: %i\n",
      //   diskExtents.Extents[0].DiskNumber);
      return -1;
    }
    diskNumber = diskExtents.Extents[0].DiskNumber;
  } else {
    errorCode = GetLastError();
    // printf("[INFO] VOLUME_GET_VOLUME_DISK_EXTENTS: Error 0x%08lX\n",
    //   errorCode);
  }

  result = DeviceIoControl(
    hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
    &deviceNumber, sizeof(deviceNumber), &size, NULL);

  if (result) {
    // printf("[INFO] DeviceNumber: %i\n", deviceNumber.DeviceNumber);
    diskNumber = deviceNumber.DeviceNumber;
  }

  // errorCode = GetLastError();
  // printf("[INFO] STORAGE_GET_DEVICE_NUMBER: Error 0x%08lX\n", errorCode);

  return diskNumber;
}

void GetMountpoints(int32_t deviceNumber,
  std::vector<std::string> *mountpoints) {
  HANDLE hLogical = INVALID_HANDLE_VALUE;
  int32_t logicalVolumeDeviceNumber = -1;
  UINT driveType;

  std::vector<std::string> logicalVolumes = GetAvailableVolumes();

  for (std::string volumeName : logicalVolumes) {
    if (hLogical != INVALID_HANDLE_VALUE) {
      CloseHandle(hLogical);
      hLogical = INVALID_HANDLE_VALUE;
    }

    // NOTE: Ignore everything that's not a fixed or removable drive,
    // as device numbers are not unique across storage type drivers (!?),
    // and this would otherwise cause CD/DVD drive letters to be
    // attributed to blockdevices
    driveType = GetDriveTypeA((volumeName + ":\\").c_str());

    // printf("[INFO] Checking %s:/\n", volumeName.c_str());

    if ((driveType != DRIVE_FIXED) && (driveType != DRIVE_REMOVABLE)) {
      // printf("[INFO] Ignoring volume %s:/ - Not FIXED | REMOVABLE\n",
      //   volumeName.c_str());
      continue;
    }

    hLogical = CreateFileA(
      ("\\\\.\\" + volumeName + ":").c_str(), 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hLogical == INVALID_HANDLE_VALUE) {
      // printf("[INFO] Couldn't open handle to logical volume %s\n",
      //   volumeName.c_str());
      continue;
    }

    logicalVolumeDeviceNumber = GetDeviceNumber(hLogical);

    if (logicalVolumeDeviceNumber == -1) {
      // printf("[INFO] Couldn't get device number for logical volume %s\n",
      //   volumeName.c_str());
      continue;
    }

    if (logicalVolumeDeviceNumber == deviceNumber) {
      // printf("[INFO] Device number for logical volume %s is %i\n",
      //   volumeName.c_str(), logicalVolumeDeviceNumber);
      mountpoints->push_back(volumeName + ":\\");
    }
  }

  if (hLogical != INVALID_HANDLE_VALUE) {
    CloseHandle(hLogical);
    hLogical = INVALID_HANDLE_VALUE;
  }
}

std::string GetBusType(STORAGE_ADAPTER_DESCRIPTOR *adapterDescriptor) {
  switch (adapterDescriptor->BusType) {
    case STORAGE_BUS_TYPE::BusTypeUnknown: return "UNKNOWN";
    case STORAGE_BUS_TYPE::BusTypeScsi: return "SCSI";
    case STORAGE_BUS_TYPE::BusTypeAtapi: return "ATAPI";
    case STORAGE_BUS_TYPE::BusTypeAta: return "ATA";
    case STORAGE_BUS_TYPE::BusType1394: return "1394";  // IEEE 1394
    case STORAGE_BUS_TYPE::BusTypeSsa: return "SSA";
    case STORAGE_BUS_TYPE::BusTypeFibre: return "FIBRE";
    case STORAGE_BUS_TYPE::BusTypeUsb: return "USB";
    case STORAGE_BUS_TYPE::BusTypeRAID: return "RAID";
    case STORAGE_BUS_TYPE::BusTypeiScsi: return "iSCSI";
    case STORAGE_BUS_TYPE::BusTypeSas: return "SAS";  // Serial-Attached SCSI
    case STORAGE_BUS_TYPE::BusTypeSata: return "SATA";
    case STORAGE_BUS_TYPE::BusTypeSd: return "SD";  // Secure Digital (SD)
    case STORAGE_BUS_TYPE::BusTypeMmc: return "MMC";  // Multimedia card
    case STORAGE_BUS_TYPE::BusTypeVirtual: return "VIRTUAL";
    case STORAGE_BUS_TYPE::BusTypeFileBackedVirtual: return "FILEBACKEDVIRTUAL";
    default: return "INVALID";
  }
}

bool GetAdapterInfo(HANDLE hPhysical, DeviceDescriptor *device) {
  STORAGE_PROPERTY_QUERY query;
  STORAGE_ADAPTER_DESCRIPTOR adapterDescriptor;
  DWORD size = 0;

  ZeroMemory(&query, sizeof(query));

  query.QueryType = STORAGE_QUERY_TYPE::PropertyStandardQuery;
  query.PropertyId = STORAGE_PROPERTY_ID::StorageAdapterProperty;

  BOOL hasAdapterInfo = DeviceIoControl(
    hPhysical, IOCTL_STORAGE_QUERY_PROPERTY,
    &query, sizeof(STORAGE_PROPERTY_QUERY),
    &adapterDescriptor, sizeof(STORAGE_ADAPTER_DESCRIPTOR), &size, NULL);

  if (hasAdapterInfo) {
    device->busType = GetBusType(&adapterDescriptor);
    device->busVersion = std::to_string(adapterDescriptor.BusMajorVersion) +
      "." + std::to_string(adapterDescriptor.BusMinorVersion);
    return true;
  }

  return false;
}

bool GetDeviceBlockSize(HANDLE hPhysical, DeviceDescriptor *device) {
  STORAGE_PROPERTY_QUERY query;
  STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR alignmentDescriptor;
  DWORD size = 0;

  ZeroMemory(&query, sizeof(query));

  query.QueryType = STORAGE_QUERY_TYPE::PropertyStandardQuery;
  query.PropertyId = STORAGE_PROPERTY_ID::StorageAccessAlignmentProperty;

  BOOL hasAlignmentDescriptor = DeviceIoControl(
    hPhysical, IOCTL_STORAGE_QUERY_PROPERTY,
    &query, sizeof(STORAGE_PROPERTY_QUERY),
    &alignmentDescriptor, sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR),
    &size, NULL);

  if (hasAlignmentDescriptor) {
    device->blockSize = alignmentDescriptor.BytesPerPhysicalSector;
    device->logicalBlockSize = alignmentDescriptor.BytesPerLogicalSector;
    return true;
  }

  return false;
}

bool GetDeviceSize(HANDLE hPhysical, DeviceDescriptor *device) {
  DISK_GEOMETRY_EX diskGeometry;

  // printf("[INFO] hasDiskGeometry\n");

  BOOL hasDiskGeometry = false;
  DWORD size = 0;

  hasDiskGeometry = DeviceIoControl(
    hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
    &diskGeometry, sizeof(DISK_GEOMETRY_EX), &size, NULL);

  // printf("[INFO] hasDiskGeometry %i\n", hasDiskGeometry);

  // NOTE: Another way to get the block size would be
  // `IOCTL_STORAGE_QUERY_PROPERTY` with `STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR`,
  // which can yield more (or possibly more accurate?) info,
  // but might not work with external HDDs/SSDs
  if (hasDiskGeometry) {
    device->size = diskGeometry.DiskSize.QuadPart;
    device->blockSize = diskGeometry.Geometry.BytesPerSector;
  }

  return hasDiskGeometry;
}

bool GetDetailData(DeviceDescriptor* device,
  HDEVINFO hDeviceInfo, SP_DEVINFO_DATA deviceInfoData) {
  DWORD index;
  DWORD size;
  DWORD errorCode = 0;
  bool result = true;

  HANDLE hDevice = INVALID_HANDLE_VALUE;
  HANDLE hPhysical = INVALID_HANDLE_VALUE;
  SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
  PSP_DEVICE_INTERFACE_DETAIL_DATA_W deviceDetailData(NULL);

  for (index = 0; ; index++) {
    if (hDevice != INVALID_HANDLE_VALUE) {
      CloseHandle(hDevice);
      hDevice = INVALID_HANDLE_VALUE;
    }

    if (hPhysical != INVALID_HANDLE_VALUE) {
      CloseHandle(hPhysical);
      hPhysical = INVALID_HANDLE_VALUE;
    }

    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // printf("[INFO] (%i) SetupDiEnumDeviceInterfaces - isDisk\n", index);

    BOOL isDisk = SetupDiEnumDeviceInterfaces(
      hDeviceInfo, &deviceInfoData, &GUID_DEVICE_INTERFACE_DISK,
      index, &deviceInterfaceData);

    if (!isDisk) {
      errorCode = GetLastError();
      if (errorCode == ERROR_NO_MORE_ITEMS) {
        // printf("[INFO] (%i) EnumDeviceInterfaces: No more items 0x%08lX\n",
        //   index, errorCode);
        result = index != 0;
        break;
      } else {
        device->error = "SetupDiEnumDeviceInterfaces: Error " +
          std::to_string(errorCode);
      }
      result = false;
      break;
    }

    BOOL hasDeviceInterfaceData = SetupDiGetDeviceInterfaceDetailW(
      hDeviceInfo, &deviceInterfaceData, NULL, 0, &size, NULL);

    if (!hasDeviceInterfaceData) {
      errorCode = GetLastError();
      if (errorCode == ERROR_INSUFFICIENT_BUFFER) {
        deviceDetailData = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>
          (malloc(size));
        if (deviceDetailData == NULL) {
          device->error = "SetupDiGetDeviceInterfaceDetailW: "
            "Unable to allocate SP_DEVICE_INTERFACE_DETAIL_DATA_W"
            "(" + std::to_string(size) + "); "
            "Error " + std::to_string(errorCode);
          result = false;
          break;
        }
        // printf("Allocated SP_DEVICE_INTERFACE_DETAIL_DATA_W\n");
      } else {
        device->error = "SetupDiGetDeviceInterfaceDetailW: "
          "Couldn't get detail data; Error " + std::to_string(errorCode);
        result = false;
        break;
      }
    }

    // printf("[INFO] (%i) Getting SP_DEVICE_INTERFACE_DETAIL_DATA_W\n", index);

    deviceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    BOOL hasDeviceDetail = SetupDiGetDeviceInterfaceDetailW(
      hDeviceInfo, &deviceInterfaceData, deviceDetailData, size, &size, NULL);

    if (!hasDeviceDetail) {
      errorCode = GetLastError();
      device->error = "Couldn't SetupDiGetDeviceInterfaceDetailW: Error " +
        std::to_string(errorCode);
      result = false;
      break;
    }

    // printf("[INFO] (%i) SetupDiGetDeviceInterfaceDetailW:\n %s\n",
    //   index, WCharToUtf8(deviceDetailData->DevicePath));

    // Passing zero to `CreateFile()` doesn't require permissions to
    // open the device handle, but only lets you acquire device metadata,
    // which is all we want
    hDevice = CreateFileW(
      deviceDetailData->DevicePath, 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
      errorCode = GetLastError();
      // printf("[ERROR] Couldn't open handle to device: Error %i", errorCode);
      device->error = "Couldn't open handle to device: Error " +
        std::to_string(errorCode);
      result = false;
      break;
    }

    int32_t deviceNumber = GetDeviceNumber(hDevice);

    if (deviceNumber == -1) {
      device->error = "Couldn't get device number";
      result = false;
      break;
    }

    device->raw = "\\\\.\\PhysicalDrive" + std::to_string(deviceNumber);
    device->device = device->raw;

    GetMountpoints(deviceNumber, &device->mountpoints);

    // printf("[INFO] Opening handle to %s\n", device->raw.c_str());

    hPhysical = CreateFileA(
      device->raw.c_str(), 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    // printf("[INFO] Opened handle to %s\n", device->raw.c_str());
    // printf("[INFO] Handle %i (INVALID %i)\n",
    //   hPhysical, INVALID_HANDLE_VALUE);

    if (hPhysical == INVALID_HANDLE_VALUE) {
      errorCode = GetLastError();
      // printf("[INFO] Couldn't open handle to %s: Error %i\n",
      //   device->raw.c_str(), errorCode);
      device->error = "Couldn't open handle to physical device: Error " +
        std::to_string(errorCode);
      result = false;
      break;
    }

    // printf("[INFO] GetDeviceSize( %s )\n", device->raw.c_str());

    if (!GetDeviceSize(hPhysical, device)) {
      errorCode = GetLastError();
      // printf("[ERROR] Couldn't get disk geometry: Error %i\n", errorCode);
      device->error = "Couldn't get disk geometry: Error " +
        std::to_string(errorCode);
      result = false;
      break;
    }

    // printf("[INFO] GetAdapterInfo( %s )\n", device->raw.c_str());

    if (!GetAdapterInfo(hPhysical, device)) {
      errorCode = GetLastError();
      // printf("[ERROR] Couldn't get device adapter descriptor: Error %i\n",
      //   errorCode);
      device->error = "Couldn't get device adapter descriptor: Error " +
        std::to_string(errorCode);
      result = false;
      break;
    }

    // printf("[INFO] GetDeviceBlockSize( %s )\n", device->raw.c_str());

    // NOTE: No need to fail over this one,
    // as we can safely default to a 512B block size
    if (!GetDeviceBlockSize(hPhysical, device)) {
      errorCode = GetLastError();
      // printf("[INFO] Couldn't get block size: Error %u\n", errorCode);
    }

    // printf("[INFO] isWritable( %s )\n", device->raw.c_str());

    BOOL isWritable = DeviceIoControl(
      hPhysical, IOCTL_DISK_IS_WRITABLE, NULL, 0,
      NULL, 0, &size, NULL);

    device->isReadOnly = !isWritable;
  }  // end for (index = 0; ; index++)

  if (hDevice != INVALID_HANDLE_VALUE) {
    CloseHandle(hDevice);
    hDevice = INVALID_HANDLE_VALUE;
  }

  if (hPhysical != INVALID_HANDLE_VALUE) {
    CloseHandle(hPhysical);
    hPhysical = INVALID_HANDLE_VALUE;
  }

  free(deviceDetailData);

  return result;
}

std::vector<DeviceDescriptor> ListStorageDevices() {
  HDEVINFO hDeviceInfo = NULL;
  SP_DEVINFO_DATA deviceInfoData;
  std::vector<DeviceDescriptor> deviceList;

  DWORD i;
  std::string enumeratorName;
  DeviceDescriptor device;

  hDeviceInfo = SetupDiGetClassDevsA(
    &GUID_DEVICE_INTERFACE_DISK, NULL, NULL,
    DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

  if (hDeviceInfo == INVALID_HANDLE_VALUE) {
    printf("[ERROR] Invalid DeviceInfo handle\n");
    return deviceList;
  }

  deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  for (i = 0; SetupDiEnumDeviceInfo(hDeviceInfo, i, &deviceInfoData); i++) {
    enumeratorName = GetEnumeratorName(hDeviceInfo, deviceInfoData);

    printf("[INFO] Enumerating %s\n", enumeratorName.c_str());

    // If it failed to get the SPDRP_ENUMERATOR_NAME, skip it
    //if (enumeratorName.empty()) {
    //  continue;
    //}

    device = DeviceDescriptor();

    device.enumerator = enumeratorName;
    device.description = GetFriendlyName(hDeviceInfo, deviceInfoData);
    device.isRemovable = IsRemovableDevice(hDeviceInfo, deviceInfoData);
    device.isVirtual = IsVirtualHardDrive(hDeviceInfo, deviceInfoData);
    device.isSCSI = IsSCSIDevice(enumeratorName);
    device.isUSB = IsUSBDevice(enumeratorName);
    device.isCard = device.enumerator == "SD";
    device.isSystem = !device.isRemovable &&
      (device.enumerator == "SCSI" || device.enumerator == "IDE");
    device.isUAS = device.isSCSI && device.isRemovable &&
      !device.isVirtual && !device.isCard;
    device.devicePathNull = true;

    if (GetDetailData(&device, hDeviceInfo, deviceInfoData)) {
      device.isSystem = device.isSystem || IsSystemDevice(hDeviceInfo, &device);
      device.isCard = device.busType == "SD" || device.busType == "MMC";
      device.isUAS = device.enumerator == "SCSI" && device.busType == "USB";
      device.isVirtual = device.isVirtual ||
        device.busType == "VIRTUAL" ||
        device.busType == "FILEBACKEDVIRTUAL";
    } else if (device.error == "") {
      device.error = "Couldn't get detail data";
    }

    deviceList.push_back(device);
  }

  SetupDiDestroyDeviceInfoList(hDeviceInfo);

  return deviceList;
}

}  // namespace Drivelist
