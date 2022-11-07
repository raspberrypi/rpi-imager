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

#ifndef SRC_DRIVELIST_HPP_
#define SRC_DRIVELIST_HPP_

#include <nan.h>
#include <string>
#include <vector>

namespace Drivelist {

struct MountPoint {
  std::string path;
};

struct DeviceDescriptor {
  std::string enumerator;
  std::string busType;
  std::string busVersion;
  bool busVersionNull;
  std::string device;
  std::string devicePath;
  bool devicePathNull;
  std::string raw;
  std::string description;
  std::string error;
  std::string parentDevice;
  uint64_t size;
  uint32_t blockSize = 512;
  uint32_t logicalBlockSize = 512;
  std::vector<std::string> mountpoints;
  std::vector<std::string> mountpointLabels;
  std::vector<std::string> childDevices;
  bool isReadOnly;  // Device is read-only
  bool isSystem;  // Device is a system drive
  bool isVirtual;  // Device is a virtual storage device
  bool isRemovable;  // Device is removable from the running system
  bool isCard;  // Device is an SD-card
  bool isSCSI;  // Connected via the Small Computer System Interface (SCSI)
  bool isUSB;  // Connected via Universal Serial Bus (USB)
  bool isUAS;  // Connected via the USB Attached SCSI (UAS)
  bool isUASNull;
};

std::vector<DeviceDescriptor> ListStorageDevices();
v8::Local<v8::Object> PackDriveDescriptor(const DeviceDescriptor *instance);

}  // namespace Drivelist

NAN_METHOD(list);

#endif  // SRC_DRIVELIST_HPP_
