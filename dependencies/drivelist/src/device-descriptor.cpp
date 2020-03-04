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

#include <nan.h>
#include "drivelist.hpp"

using v8::String;
using v8::Number;
using v8::Boolean;
using v8::Local;
using v8::Value;
using Nan::New;

namespace Drivelist {

v8::Local<v8::Object> PackDriveDescriptor(const DeviceDescriptor *instance) {
  v8::Local<v8::Object> object = Nan::New<v8::Object>();

  Nan::Set(object,
    New<String>("enumerator").ToLocalChecked(),
    New<String>(instance->enumerator).ToLocalChecked());

  Nan::Set(object,
    New<String>("busType").ToLocalChecked(),
    New<String>(instance->busType).ToLocalChecked());

  Local<Value> busVersion = instance->busVersionNull ?
    (Local<Value>)Nan::Null() :
    (Local<Value>)New<String>(instance->busVersion).ToLocalChecked();

  Nan::Set(object,
      New<String>("busVersion").ToLocalChecked(),
      busVersion);

  Nan::Set(object,
    New<String>("device").ToLocalChecked(),
    New<String>(instance->device).ToLocalChecked());

  Local<Value> devicePath = instance->devicePathNull ?
    (Local<Value>)Nan::Null() :
    (Local<Value>)New<String>(instance->devicePath).ToLocalChecked();

  Nan::Set(object,
    New<String>("devicePath").ToLocalChecked(),
    devicePath);

  Nan::Set(object,
    New<String>("raw").ToLocalChecked(),
    New<String>(instance->raw).ToLocalChecked());

  Nan::Set(object,
    New<String>("description").ToLocalChecked(),
    New<String>(instance->description).ToLocalChecked());

  if (instance->error != "") {
    Nan::Set(object,
      New<String>("error").ToLocalChecked(),
      New<String>(instance->error).ToLocalChecked());
  } else {
    Nan::Set(object,
      New<String>("error").ToLocalChecked(),
      Nan::Null());
  }

  Nan::Set(object,
    New<String>("size").ToLocalChecked(),
    New<Number>(static_cast<double>(instance->size)));

  Nan::Set(object,
    New<String>("blockSize").ToLocalChecked(),
    New<Number>(static_cast<double>(instance->blockSize)));

  Nan::Set(object,
    New<String>("logicalBlockSize").ToLocalChecked(),
    New<Number>(static_cast<double>(instance->logicalBlockSize)));

  v8::Local<v8::Object> mountpoints = Nan::New<v8::Array>();

  uint32_t index = 0;
  for (std::string mountpointPath : instance->mountpoints) {
    v8::Local<v8::Object> mountpoint = Nan::New<v8::Object>();
    Nan::Set(mountpoint,
      New<String>("path").ToLocalChecked(),
      New<String>(mountpointPath).ToLocalChecked());

    if (index < instance->mountpointLabels.size()) {
      Nan::Set(mountpoint,
        New<String>("label").ToLocalChecked(),
        New<String>(instance->mountpointLabels[index]).ToLocalChecked());
    }

    Nan::Set(mountpoints, index, mountpoint);
    index++;
  }

  Nan::Set(object,
    New<String>("mountpoints").ToLocalChecked(),
    mountpoints);

  Nan::Set(object,
    New<String>("isReadOnly").ToLocalChecked(),
    New<Boolean>(instance->isReadOnly));

  Nan::Set(object,
    New<String>("isSystem").ToLocalChecked(),
    New<Boolean>(instance->isSystem));

  Nan::Set(object,
    New<String>("isVirtual").ToLocalChecked(),
    New<Boolean>(instance->isVirtual));

  Nan::Set(object,
    New<String>("isRemovable").ToLocalChecked(),
    New<Boolean>(instance->isRemovable));

  Nan::Set(object,
    New<String>("isCard").ToLocalChecked(),
    New<Boolean>(instance->isCard));

  Nan::Set(object,
    New<String>("isSCSI").ToLocalChecked(),
    New<Boolean>(instance->isSCSI));

  Nan::Set(object,
    New<String>("isUSB").ToLocalChecked(),
    New<Boolean>(instance->isUSB));

  Local<Value> isUAS = instance->isUASNull ?
    (Local<Value>)Nan::Null() :
    (Local<Value>)New<Boolean>(instance->isUAS);

  Nan::Set(object,
    New<String>("isUAS").ToLocalChecked(),
    isUAS);

  return object;
}

}  // namespace Drivelist
