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
#include <vector>
#include "drivelist.hpp"

class DriveListWorker : public Nan::AsyncWorker {
 public:
  explicit DriveListWorker(Nan::Callback *callback)
    : Nan::AsyncWorker(callback), devices() {}

  ~DriveListWorker() {}

  void Execute() {
    devices = Drivelist::ListStorageDevices();
  }

  void HandleOKCallback() {
    Nan::HandleScope scope;
    v8::Local<v8::Object> drives = Nan::New<v8::Array>();

    uint32_t i;
    uint32_t size = (uint32_t) devices.size();

    for (i = 0; i < size; i++) {
      Nan::Set(drives, i, Drivelist::PackDriveDescriptor(&devices[i]));
    }

    v8::Local<v8::Value> argv[] = { Nan::Null(), drives };
    callback->Call(2, argv, async_resource);
  }

 private:
  std::vector<Drivelist::DeviceDescriptor> devices;
};

NAN_METHOD(list) {
  if (!info[0]->IsFunction()) {
    return Nan::ThrowTypeError("Callback must be a function");
  }

  Nan::Callback *callback = new Nan::Callback(info[0].As<v8::Function>());
  Nan::AsyncQueueWorker(new DriveListWorker(callback));

  info.GetReturnValue().SetUndefined();
}

NAN_MODULE_INIT(InitAll) {
  NAN_EXPORT(target, list);
}

NODE_MODULE(DriveList, InitAll);
