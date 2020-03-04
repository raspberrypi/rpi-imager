#ifndef SRC_MOUNTUTILS_HPP_
#define SRC_MOUNTUTILS_HPP_

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

#include <nan.h>
#include <string>

enum MOUNTUTILS_RESULT {
  MOUNTUTILS_SUCCESS,
  MOUNTUTILS_ERROR_INVALID_DRIVE,
  MOUNTUTILS_ERROR_ACCESS_DENIED,
  MOUNTUTILS_ERROR_AGAIN,
  MOUNTUTILS_ERROR_GENERAL
};

void MountUtilsLog(std::string string);

MOUNTUTILS_RESULT unmount_disk(const char *device);
MOUNTUTILS_RESULT eject_disk(const char *device);

NAN_METHOD(unmountDisk);
NAN_METHOD(eject);

#endif  // SRC_MOUNTUTILS_HPP_
