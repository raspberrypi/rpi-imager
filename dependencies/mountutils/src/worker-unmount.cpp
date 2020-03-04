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
#include "mountutils.hpp"

class UnmountWorker : public Nan::AsyncWorker {
 public:
  UnmountWorker(Nan::Callback *callback, std::string device)
    : Nan::AsyncWorker(callback) {
    device_path = device;
  }

  ~UnmountWorker() {}

  void Execute() {
    MOUNTUTILS_RESULT result = unmount_disk(device_path.c_str());

    MountUtilsLog("Unmount complete");

    if (result != MOUNTUTILS_SUCCESS) {
      switch (result) {
        case MOUNTUTILS_ERROR_ACCESS_DENIED:
          SetErrorMessage("Unmount failed, access denied");
          break;
        case MOUNTUTILS_ERROR_INVALID_DRIVE:
          SetErrorMessage("Unmount failed, invalid drive");
          break;
        default:
          SetErrorMessage("Unmount failed");
          break;
      }
    }
  }

  void HandleOKCallback() {
    Nan::HandleScope scope;
    v8::Local<v8::Value> argv[] = { Nan::Null() };
    callback->Call(1, argv, async_resource);
  }

 private:
  std::string device_path;
};

NAN_METHOD(unmountDisk) {
  if (!info[1]->IsFunction()) {
    return Nan::ThrowError("Callback must be a function");
  }

  if (!info[0]->IsString()) {
    return Nan::ThrowError("Device must be a string");
  }

  Nan::Utf8String device(Nan::To<v8::String>(info[0]).ToLocalChecked());
  std::string device_path(*device);
  Nan::Callback *callback = new Nan::Callback(info[1].As<v8::Function>());

  Nan::AsyncQueueWorker(new UnmountWorker(callback, device_path));

  info.GetReturnValue().SetUndefined();
}
