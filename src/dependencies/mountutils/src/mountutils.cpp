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

#include <cstdlib>
#include <iostream>
#include "mountutils.hpp"

NAN_MODULE_INIT(MountUtilsInit) {
  NAN_EXPORT(target, unmountDisk);
  NAN_EXPORT(target, eject);
}

void MountUtilsLog(std::string string) {
  const char* debug = std::getenv("MOUNTUTILS_DEBUG");
  if (debug != NULL) {
    std::cout << "[mountutils] " << string << std::endl;
  }
}

NODE_MODULE(MountUtils, MountUtilsInit)
