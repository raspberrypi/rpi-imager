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

#include <DiskArbitration/DiskArbitration.h>
#include "../mountutils.hpp"

struct RunLoopContext {
  MOUNTUTILS_RESULT code = MOUNTUTILS_SUCCESS;
};

MOUNTUTILS_RESULT translate_dissenter(DADissenterRef dissenter) {
  if (dissenter) {
    DAReturn status = DADissenterGetStatus(dissenter);
    if (status == kDAReturnBadArgument ||
        status == kDAReturnNotFound) {
      return MOUNTUTILS_ERROR_INVALID_DRIVE;
    } else if (status == kDAReturnNotPermitted ||
               status == kDAReturnNotPrivileged) {
      return MOUNTUTILS_ERROR_ACCESS_DENIED;
    } else {
      MountUtilsLog("Unknown dissenter status");
      return MOUNTUTILS_ERROR_GENERAL;
    }
  } else {
    return MOUNTUTILS_SUCCESS;
  }
}

MOUNTUTILS_RESULT
run_cb(const char* device, DADiskUnmountCallback callback, size_t times) {
  RunLoopContext context;
  void *ctx = &context;

  // Create a session object
  MountUtilsLog("Creating DA session");
  DASessionRef session = DASessionCreate(kCFAllocatorDefault);

  if (session == NULL) {
    MountUtilsLog("Session couldn't be created");
    return MOUNTUTILS_ERROR_GENERAL;
  }

  // Get a disk object from the disk path
  MountUtilsLog("Getting disk object");
  DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault,
    session, device);

  // Unmount, and then eject from the unmount callback
  MountUtilsLog("Unmounting");
  DADiskUnmount(disk,
                kDADiskUnmountOptionWhole | kDADiskUnmountOptionForce,
                callback,
                ctx);

  // Schedule a disk arbitration session
  MountUtilsLog("Schedule session on run loop");
  DASessionScheduleWithRunLoop(session,
    CFRunLoopGetCurrent(),
    kCFRunLoopDefaultMode);

  // Start the run loop: Run with a timeout of 500ms (0.5s),
  // and don't terminate after only handling one resource.
  // NOTE: As the unmount callback gets called *before* the runloop can
  // be started here when there's no device to be unmounted or
  // the device has already been unmounted, the loop would
  // hang indefinitely until stopped manually otherwise.
  // Here we repeatedly run the loop for a given time, and stop
  // it at some point if it hasn't gotten anywhere, or if there's
  // nothing to be unmounted, or a dissent has been caught before the run.
  // This way we don't have to manage state across callbacks.
  MountUtilsLog("Starting run loop");

  bool done = false;
  unsigned int loop_count = 0;

  while (!done) {
    loop_count++;
    // See https://developer.apple.com/reference/corefoundation/1541988-cfrunloopruninmode
    SInt32 status = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
    // Stop starting the runloop once it's been manually stopped
    if ((status == kCFRunLoopRunStopped) || (status == kCFRunLoopRunFinished)) {
      done = true;
    }
    // Bail out if DADiskUnmount caught a dissent and
    // thus returned before the runloop even started
    if (context.code != MOUNTUTILS_SUCCESS) {
      MountUtilsLog("Runloop dry");
      done = true;
    }
    // Bail out if the runloop is timing out, but not getting anywhere
    if (loop_count > 10) {
      MountUtilsLog("Runloop stall");
      context.code = MOUNTUTILS_ERROR_AGAIN;
      done = true;
    }
  }

  // Clean up the session
  MountUtilsLog("Releasing session & disk object");
  DASessionUnscheduleFromRunLoop(session,
    CFRunLoopGetCurrent(),
    kCFRunLoopDefaultMode);
  CFRelease(session);

  if (context.code == MOUNTUTILS_ERROR_AGAIN && times < 5) {
    MountUtilsLog("Retrying...");
    return run_cb(device, callback, times + 1);
  }

  MOUNTUTILS_RESULT result = context.code;
  return result;
}

void _unmount_cb(DADiskRef disk, DADissenterRef dissenter, void *ctx) {
  MountUtilsLog("[unmount]: Unmount callback");
  RunLoopContext *context = reinterpret_cast<RunLoopContext*>(ctx);
  if (dissenter) {
    MountUtilsLog("[unmount]: Unmount dissenter");
    context->code = translate_dissenter(dissenter);
  } else {
    MountUtilsLog("[unmount]: Unmount success");
    context->code = MOUNTUTILS_SUCCESS;
  }
  CFRunLoopStop(CFRunLoopGetCurrent());
}

void _eject_cb(DADiskRef disk, DADissenterRef dissenter, void *ctx) {
  MountUtilsLog("[eject]: Eject callback");
  RunLoopContext *context = reinterpret_cast<RunLoopContext*>(ctx);
  if (dissenter) {
    MountUtilsLog("[eject]: Eject dissenter");
    context->code = translate_dissenter(dissenter);
  } else {
    MountUtilsLog("[eject]: Eject success");
    context->code = MOUNTUTILS_SUCCESS;
  }
  CFRunLoopStop(CFRunLoopGetCurrent());
}

void _eject_unmount_cb(DADiskRef disk, DADissenterRef dissenter, void *ctx) {
  MountUtilsLog("[eject]: Unmount callback");
  RunLoopContext *context = reinterpret_cast<RunLoopContext*>(ctx);
  if (dissenter) {
    MountUtilsLog("[eject]: Unmount dissenter");
    context->code = translate_dissenter(dissenter);
    CFRunLoopStop(CFRunLoopGetCurrent());
  } else {
    MountUtilsLog("[eject]: Unmount success");
    MountUtilsLog("[eject]: Ejecting...");
    context->code = MOUNTUTILS_SUCCESS;
    DADiskEject(disk,
      kDADiskEjectOptionDefault,
      _eject_cb,
      ctx);
  }
}

MOUNTUTILS_RESULT unmount_disk(const char* device) {
  return run_cb(device, _unmount_cb, 0);
}

MOUNTUTILS_RESULT eject_disk(const char* device) {
  return run_cb(device, _eject_unmount_cb, 0);
}
