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

#ifndef SRC_WINDOWS_LIST_HPP_
#define SRC_WINDOWS_LIST_HPP_

#include <knownfolders.h>
#include <string>
#include <set>

namespace Drivelist {

// The <ntddstor.h>, and <usbiodef.h> headers include the following
// device interface GUIDs we're interested in;
// @see https://docs.microsoft.com/en-us/windows-hardware/drivers/install/install-reference
// @see https://msdn.microsoft.com/en-us/library/windows/hardware/ff541389(v=vs.85).aspx
// To avoid cluttering the global namespace,
// we'll just define here what we need:
//
// - GUID_DEVINTERFACE_DISK { 53F56307-B6BF-11D0-94F2-00A0C91EFB8B }
// - GUID_DEVINTERFACE_CDROM { 53F56308-B6BF-11D0-94F2-00A0C91EFB8B }
// - GUID_DEVINTERFACE_USB_HUB { F18A0E88-C30C-11D0-8815-00A0C906BED8 }
// - GUID_DEVINTERFACE_FLOPPY { 53F56311-B6BF-11D0-94F2-00A0C91EFB8B }
// - GUID_DEVINTERFACE_WRITEONCEDISK { 53F5630C-B6BF-11D0-94F2-00A0C91EFB8B }
// - GUID_DEVINTERFACE_TAPE { 53F5630B-B6BF-11D0-94F2-00A0C91EFB8B }
// - GUID_DEVINTERFACE_USB_DEVICE { A5DCBF10-6530-11D2-901F-00C04FB951ED }
// - GUID_DEVINTERFACE_VOLUME { 53F5630D-B6BF-11D0-94F2-00A0C91EFB8B }
// - GUID_DEVINTERFACE_STORAGEPORT { 2ACCFE60-C130-11D2-B082-00A0C91EFB8B }
//
const GUID GUID_DEVICE_INTERFACE_DISK = {
0x53F56307L, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B }
};

const GUID GUID_DEVICE_INTERFACE_CDROM = {
0x53F56308L, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B }
};

const GUID GUID_DEVICE_INTERFACE_USB_HUB = {
0xF18A0E88L, 0xC30C, 0x11D0, { 0x88, 0x15, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0xD8 }
};

const GUID GUID_DEVICE_INTERFACE_FLOPPY = {
0x53F56311L, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B }
};

const GUID GUID_DEVICE_INTERFACE_WRITEONCEDISK = {
0x53F5630CL, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B }
};

const GUID GUID_DEVICE_INTERFACE_TAPE = {
0x53F5630BL, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B }
};

const GUID GUID_DEVICE_INTERFACE_USB_DEVICE = {
0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED }
};

const GUID GUID_DEVICE_INTERFACE_VOLUME = {
0x53F5630DL, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B }
};

const GUID GUID_DEVICE_INTERFACE_STORAGEPORT = {
0x2ACCFE60L, 0xC130, 0x11D2, { 0xB0, 0x82, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B }
};

const std::set<std::string> USB_STORAGE_DRIVERS {
  "USBSTOR", "UASPSTOR", "VUSBSTOR",
  "RTUSER", "CMIUCR", "EUCR",
  "ETRONSTOR", "ASUSSTPT"
};

const std::set<std::string> GENERIC_STORAGE_DRIVERS {
  "SCSI", "SD", "PCISTOR",
  "RTSOR", "JMCR", "JMCF", "RIMMPTSK", "RIMSPTSK", "RIXDPTSK",
  "TI21SONY", "ESD7SK", "ESM7SK", "O2MD", "O2SD", "VIACR"
};

// List of known virtual disk hardware IDs
const std::set<std::string> VHD_HARDWARE_IDS {
  "Arsenal_________Virtual_",
  "KernSafeVirtual_________",
  "Msft____Virtual_Disk____",
  "VMware__VMware_Virtual_S"
};

const GUID KNOWN_FOLDER_IDS[] {
  FOLDERID_Windows,
  FOLDERID_Profile,
  FOLDERID_ProgramFiles,
  FOLDERID_ProgramFilesX86
};

}  // namespace Drivelist

#endif  // SRC_WINDOWS_LIST_HPP_
