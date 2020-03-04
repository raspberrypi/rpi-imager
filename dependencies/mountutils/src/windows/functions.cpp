/*
 * Copyright 2017 resin.io
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

// Adapted from https://support.microsoft.com/en-us/kb/165721

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winioctl.h>
#include <tchar.h>
#include <stdio.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include "../mountutils.hpp"

HANDLE CreateVolumeHandleFromDevicePath(LPCTSTR devicePath, DWORD flags) {
  return CreateFile(devicePath,
                    flags,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL);
}

HANDLE CreateVolumeHandleFromDriveLetter(TCHAR driveLetter, DWORD flags) {
  TCHAR devicePath[8];
  sprintf_s(devicePath, "\\\\.\\%c:", driveLetter);
  return CreateVolumeHandleFromDevicePath(devicePath, flags);
}

ULONG GetDeviceNumberFromVolumeHandle(HANDLE volume) {
  STORAGE_DEVICE_NUMBER storageDeviceNumber;
  DWORD bytesReturned;

  BOOL result = DeviceIoControl(volume,
                                IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                NULL, 0,
                                &storageDeviceNumber,
                                sizeof(storageDeviceNumber),
                                &bytesReturned,
                                NULL);

  if (!result) {
    return 0;
  }

  return storageDeviceNumber.DeviceNumber;
}

BOOL IsDriveFixed(TCHAR driveLetter) {
  TCHAR rootName[5];
  sprintf_s(rootName, "%c:\\", driveLetter);
  return GetDriveType(rootName) == DRIVE_FIXED;
}

BOOL LockVolume(HANDLE volume) {
  DWORD bytesReturned;

  for (size_t tries = 0; tries < 20; tries++) {
    if (DeviceIoControl(volume,
                        FSCTL_LOCK_VOLUME,
                        NULL, 0,
                        NULL, 0,
                        &bytesReturned,
                        NULL)) {
      return TRUE;
    }

    Sleep(500);
  }

  return FALSE;
}

// Adapted from https://www.codeproject.com/articles/13839/how-to-prepare-a-usb-drive-for-safe-removal
// which is licensed under "The Code Project Open License (CPOL) 1.02"
// https://www.codeproject.com/info/cpol10.aspx
DEVINST GetDeviceInstanceFromDeviceNumber(ULONG deviceNumber) {
  const GUID* guid = reinterpret_cast<const GUID *>(&GUID_DEVINTERFACE_DISK);

  // Get device interface info set handle for all devices attached to system
  DWORD deviceInformationFlags = DIGCF_PRESENT | DIGCF_DEVICEINTERFACE;
  HDEVINFO deviceInformation = SetupDiGetClassDevs(guid,
                                                   NULL, NULL,
                                                   deviceInformationFlags);

  if (deviceInformation == INVALID_HANDLE_VALUE)  {
    return 0;
  }

  DWORD memberIndex = 0;
  BYTE buffer[1024];

  PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData =
    (PSP_DEVICE_INTERFACE_DETAIL_DATA)buffer;

  SP_DEVINFO_DATA deviceInformationData;
  DWORD requiredSize;

  SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
  deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  while (true) {
    if (!SetupDiEnumDeviceInterfaces(deviceInformation,
                                     NULL,
                                     guid,
                                     memberIndex,
                                     &deviceInterfaceData)) {
      break;
    }

    requiredSize = 0;
    SetupDiGetDeviceInterfaceDetail(deviceInformation,
                                    &deviceInterfaceData,
                                    NULL, 0,
                                    &requiredSize, NULL);

    if (requiredSize == 0 || requiredSize > sizeof(buffer)) {
      memberIndex++;
      continue;
    }

    deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    ZeroMemory((PVOID)&deviceInformationData, sizeof(SP_DEVINFO_DATA));
    deviceInformationData.cbSize = sizeof(SP_DEVINFO_DATA);

    BOOL result = SetupDiGetDeviceInterfaceDetail(deviceInformation,
                                                  &deviceInterfaceData,
                                                  deviceInterfaceDetailData,
                                                  sizeof(buffer),
                                                  &requiredSize,
                                                  &deviceInformationData);

    if (!result) {
      memberIndex++;
      continue;
    }

    LPCTSTR devicePath = deviceInterfaceDetailData->DevicePath;
    HANDLE driveHandle = CreateVolumeHandleFromDevicePath(devicePath, 0);

    if (driveHandle == INVALID_HANDLE_VALUE) {
      memberIndex++;
      continue;
    }

    ULONG currentDriveDeviceNumber =
      GetDeviceNumberFromVolumeHandle(driveHandle);

    CloseHandle(driveHandle);

    if (!currentDriveDeviceNumber) {
      memberIndex++;
      continue;
    }

    if (deviceNumber == currentDriveDeviceNumber) {
      SetupDiDestroyDeviceInfoList(deviceInformation);
      return deviceInformationData.DevInst;
    }

    memberIndex++;
  }

  SetupDiDestroyDeviceInfoList(deviceInformation);

  return 0;
}

BOOL UnlockVolume(HANDLE volume) {
  DWORD bytesReturned;

  return DeviceIoControl(volume,
                         FSCTL_UNLOCK_VOLUME,
                         NULL, 0,
                         NULL, 0,
                         &bytesReturned,
                         NULL);
}

BOOL DismountVolume(HANDLE volume) {
  DWORD bytesReturned;

  return DeviceIoControl(volume,
                         FSCTL_DISMOUNT_VOLUME,
                         NULL, 0,
                         NULL, 0,
                         &bytesReturned,
                         NULL);
}

BOOL IsVolumeMounted(HANDLE volume) {
  DWORD bytesReturned;

  return DeviceIoControl(volume,
                         FSCTL_IS_VOLUME_MOUNTED,
                         NULL, 0,
                         NULL, 0,
                         &bytesReturned,
                         NULL);
}

BOOL EjectRemovableVolume(HANDLE volume) {
  DWORD bytesReturned;
  PREVENT_MEDIA_REMOVAL buffer;
  buffer.PreventMediaRemoval = FALSE;
  BOOL result = DeviceIoControl(volume,
                                IOCTL_STORAGE_MEDIA_REMOVAL,
                                &buffer, sizeof(PREVENT_MEDIA_REMOVAL),
                                NULL, 0,
                                &bytesReturned,
                                NULL);

  if (!result) {
    MountUtilsLog("Couldn't prevent media removal");
    return FALSE;
  }

  for (size_t tries = 0; tries < 5; tries++) {
    if (tries != 0) {
      MountUtilsLog("Retrying ejection");
      Sleep(500);
    }
    const BOOL result = DeviceIoControl(volume,
                                        IOCTL_STORAGE_EJECT_MEDIA,
                                        NULL, 0,
                                        NULL, 0,
                                        &bytesReturned,
                                        NULL);
    if (result) {
      MountUtilsLog("Volume ejected");
      return TRUE;
    }
  }

  return FALSE;
}

MOUNTUTILS_RESULT EjectFixedDriveByDeviceNumber(ULONG deviceNumber) {
  DEVINST deviceInstance = GetDeviceInstanceFromDeviceNumber(deviceNumber);
  if (!deviceInstance) {
    MountUtilsLog("Couldn't get instance from device number");
    return MOUNTUTILS_ERROR_GENERAL;
  }

  CONFIGRET status;
  PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
  char vetoName[MAX_PATH];

  // It's often seen that the removal fails on the first
  // attempt but works on the second attempt.
  // See https://www.codeproject.com/articles/13839/how-to-prepare-a-usb-drive-for-safe-removal
  for (size_t tries = 0; tries < 3; tries++) {
    if (tries != 0) {
      MountUtilsLog("Retrying");
      Sleep(500);
    }
    MountUtilsLog("Ejecting device instance");
    status = CM_Request_Device_Eject(deviceInstance,
                                     &vetoType,
                                     vetoName,
                                     MAX_PATH,
                                     0);

    if (status == CR_SUCCESS) {
      MountUtilsLog("Ejected device instance successfully");
      return MOUNTUTILS_SUCCESS;
    }

    MountUtilsLog("Ejecting was vetoed");

    // We use this as an indicator that the device driver
    // is not setting the `SurpriseRemovalOK` capability.
    // See https://msdn.microsoft.com/en-us/library/windows/hardware/ff539722(v=vs.85).aspx
    if (status == CR_REMOVE_VETOED &&
        vetoType == PNP_VetoIllegalDeviceRequest) {
      MountUtilsLog("Removing subtree");
      status = CM_Query_And_Remove_SubTree(deviceInstance,
                                           &vetoType,
                                           vetoName,
                                           MAX_PATH,

      // We have to add the `CM_REMOVE_NO_RESTART` flag because
      // otherwise the just-removed device may be immediately
      // redetected, which might happen on XP and Vista.
      // See https://www.codeproject.com/articles/13839/how-to-prepare-a-usb-drive-for-safe-removal

                                           CM_REMOVE_NO_RESTART);

      if (status == CR_ACCESS_DENIED) {
        return MOUNTUTILS_ERROR_ACCESS_DENIED;
      }

      if (status == CR_SUCCESS) {
        return MOUNTUTILS_SUCCESS;
      }

      MountUtilsLog("Couldn't eject device instance");
      return MOUNTUTILS_ERROR_GENERAL;
    }
  }

  return MOUNTUTILS_ERROR_GENERAL;
}

MOUNTUTILS_RESULT EjectDriveLetter(TCHAR driveLetter) {
  DWORD volumeFlags = GENERIC_READ | GENERIC_WRITE;
  HANDLE volumeHandle = CreateVolumeHandleFromDriveLetter(driveLetter,
                                                          volumeFlags);

  MountUtilsLog("Creating volume handle");

  if (volumeHandle == INVALID_HANDLE_VALUE) {
    MountUtilsLog("Couldn't create volume handle");
    return MOUNTUTILS_ERROR_INVALID_DRIVE;
  }

  // Don't proceed if the volume is not mounted
  if (!IsVolumeMounted(volumeHandle)) {
    MountUtilsLog("Volume is not mounted");

    if (!CloseHandle(volumeHandle)) {
      MountUtilsLog("Couldn't close volume handle");
      return MOUNTUTILS_ERROR_GENERAL;
    }

    return MOUNTUTILS_SUCCESS;
  }

  if (IsDriveFixed(driveLetter)) {
    MountUtilsLog("Drive is fixed");

    ULONG deviceNumber = GetDeviceNumberFromVolumeHandle(volumeHandle);
    if (!deviceNumber) {
      MountUtilsLog("Couldn't get device number from volume handle");
      CloseHandle(volumeHandle);
      return MOUNTUTILS_ERROR_GENERAL;
    }

    if (!CloseHandle(volumeHandle)) {
      MountUtilsLog("Couldn't close volume handle");
      return MOUNTUTILS_ERROR_GENERAL;
    }

    MountUtilsLog("Ejecting fixed drive");
    return EjectFixedDriveByDeviceNumber(deviceNumber);
  }

  MountUtilsLog("Locking volume");

  if (!LockVolume(volumeHandle)) {
    MountUtilsLog("Couldn't lock volume");
    CloseHandle(volumeHandle);
    return MOUNTUTILS_ERROR_GENERAL;
  }

  MountUtilsLog("Dismounting volume");

  if (!DismountVolume(volumeHandle)) {
    MountUtilsLog("Couldn't dismount volume");
    CloseHandle(volumeHandle);
    return MOUNTUTILS_ERROR_GENERAL;
  }

  MountUtilsLog("Ejecting volume");

  if (!EjectRemovableVolume(volumeHandle)) {
    MountUtilsLog("Couldn't eject volume");
    CloseHandle(volumeHandle);
    return MOUNTUTILS_ERROR_GENERAL;
  }

  MountUtilsLog("Unlocking volume");

  if (!UnlockVolume(volumeHandle)) {
    MountUtilsLog("Couldn't unlock volume");
    CloseHandle(volumeHandle);
    return MOUNTUTILS_ERROR_GENERAL;
  }

  MountUtilsLog("Closing volume handle");

  if (!CloseHandle(volumeHandle)) {
    MountUtilsLog("Couldn't close volume handle");
    return MOUNTUTILS_ERROR_GENERAL;
  }

  return MOUNTUTILS_SUCCESS;
}

BOOL IsDriveEjectable(TCHAR driveLetter) {
  TCHAR devicePath[8];
  sprintf_s(devicePath, "%c:\\", driveLetter);

  MountUtilsLog("Checking whether drive is ejectable: "
      + std::string(1, driveLetter));

  switch (GetDriveType(devicePath)) {
    case DRIVE_NO_ROOT_DIR:
      MountUtilsLog("The drive doesn't exist");
      return FALSE;
    case DRIVE_REMOVABLE:
      MountUtilsLog("The drive is removable");
      return TRUE;
    case DRIVE_FIXED:
      MountUtilsLog("The drive is fixed");
      return TRUE;
    case DRIVE_REMOTE:
      MountUtilsLog("The drive is remote");
      return FALSE;
    case DRIVE_CDROM:
      MountUtilsLog("The drive is a CDROM");
      return FALSE;
    case DRIVE_RAMDISK:
      MountUtilsLog("The drive is a RAM disk");
      return FALSE;
    default:
      MountUtilsLog("The drive type is unknown");
      return FALSE;
  }
}

MOUNTUTILS_RESULT Eject(ULONG deviceNumber) {
  DWORD logicalDrivesMask = GetLogicalDrives();
  TCHAR currentDriveLetter = 'A';

  if (logicalDrivesMask == 0) {
    MountUtilsLog("Couldn't get logical drives");
    return MOUNTUTILS_ERROR_GENERAL;
  }

  while (logicalDrivesMask) {
    if (logicalDrivesMask & 1 && IsDriveEjectable(currentDriveLetter)) {
      MountUtilsLog("Opening drive letter handle: "
          + std::string(1, currentDriveLetter));

      HANDLE driveHandle =
        CreateVolumeHandleFromDriveLetter(currentDriveLetter, 0);

      if (driveHandle == INVALID_HANDLE_VALUE) {
        MountUtilsLog("Couldn't open drive letter handle");
        return MOUNTUTILS_ERROR_GENERAL;
      }

      ULONG currentDeviceNumber = GetDeviceNumberFromVolumeHandle(driveHandle);

      MountUtilsLog("Closing drive letter handle");

      if (!CloseHandle(driveHandle)) {
        MountUtilsLog("Couldn't close drive letter handle");
        return MOUNTUTILS_ERROR_GENERAL;
      }

      if (currentDeviceNumber == deviceNumber) {
        MountUtilsLog("Drive letter device matches");
        MOUNTUTILS_RESULT result;

        // Retry ejecting 3 times, since I've seen that in some systems
        // the filesystem is ejected, but the drive letter remains assigned,
        // which gets fixed if you retry again.
        for (size_t times = 0; times < 3; times++) {
          if (times != 0) {
            MountUtilsLog("Retrying");
            Sleep(500);
          }
          MountUtilsLog("Ejecting drive letter");
          result = EjectDriveLetter(currentDriveLetter);

          // Abort the loop if we couldn't open a handle on the drive letter
          // after previous attempts worked, since this means the drive was
          // completely ejected, and that we don't have to keep retrying.
          if (times > 0 && result == MOUNTUTILS_ERROR_INVALID_DRIVE) {
            MountUtilsLog("Drive letter has already been ejected");
            break;
          }

          if (result != MOUNTUTILS_SUCCESS) {
            MountUtilsLog("Couldn't eject drive letter");
            return result;
          }
        }
      }

      MountUtilsLog("Continuing with the next available letter");
    }

    currentDriveLetter++;
    logicalDrivesMask >>= 1;
  }

  return MOUNTUTILS_SUCCESS;
}

// From http://stackoverflow.com/a/12923949
MOUNTUTILS_RESULT stringToInteger(char *string, int *out) {
  if (string[0] == '\0' || isspace((unsigned char) string[0])) {
    return MOUNTUTILS_ERROR_GENERAL;
  }

  char *end;
  errno = 0;
  int result = strtol(string, &end, 10);

  if (result > INT_MAX || result < INT_MIN ||
      (errno == ERANGE && result == LONG_MAX) ||
      (errno == ERANGE && result == LONG_MIN)) {
    return MOUNTUTILS_ERROR_GENERAL;
  }

  if (*end != '\0') {
    return MOUNTUTILS_ERROR_GENERAL;
  }

  *out = result;

  return MOUNTUTILS_SUCCESS;
}

MOUNTUTILS_RESULT unmount_disk(const char *device) {
  int deviceId;
  char prefix[18];

  MountUtilsLog(std::string(device));

  int result = sscanf(device, "%17s%i", prefix, &deviceId);

  // Return value of `sscanf` is the number of receiving arguments
  // successfully assigned; and `0` or `EOF` in case of failure
  if (result != 2 || result == EOF) {
    return MOUNTUTILS_ERROR_INVALID_DRIVE;
  }

  return Eject(deviceId);
}

// FIXME: This is just a stub copy of `UnmountDisk()`,
// and needs implementation!
MOUNTUTILS_RESULT eject_disk(const char *device) {
  return unmount_disk(device);
}
