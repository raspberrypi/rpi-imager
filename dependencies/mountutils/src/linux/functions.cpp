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

#include <sys/stat.h>
#include <sys/mount.h>
#include <mntent.h>
#include <errno.h>
#include "../mountutils.hpp"

MOUNTUTILS_RESULT unmount_disk(const char *device_path) {
  const char *mount_path = NULL;
  std::vector<std::string> mount_dirs = {};

  // Stat the device to make sure it exists
  struct stat stats;

  if (stat(device_path, &stats) != 0) {
    MountUtilsLog("Stat failed");

    // TODO(jhermsmeier): See TODO below
    // v8::Local<v8::Value> argv[1] = {
    //   Nan::ErrnoException(errno, "stat", NULL, device_path)
    // };
    // Nan::MakeCallback(Nan::GetCurrentContext()->Global(), callback, 1, argv);
    return MOUNTUTILS_ERROR_GENERAL;
  } else if (S_ISDIR(stats.st_mode)) {
    MountUtilsLog("Device is a directory");

    // TODO(jhermsmeier): See TODO below
    // v8::Local<v8::Value> argv[1] = {
    //   Nan::Error("Invalid device, path is a directory")
    // };
    // Nan::MakeCallback(Nan::GetCurrentContext()->Global(), callback, 1, argv);
    return MOUNTUTILS_ERROR_INVALID_DRIVE;
  }

  // Get mountpaths from the device path, as `umount(device)`
  // has been removed in Linux 2.3+
  struct mntent *mnt_p, data;
  // See https://github.com/RasPlex/aufs-utils/commit/2d1a37468cdc1f9c779cbf22267c5ae491a44f8e
  char mnt_buf[4096 + 1024];
  FILE *proc_mounts;

  MountUtilsLog("Reading /proc/mounts");
  proc_mounts = setmntent("/proc/mounts", "r");

  if (proc_mounts == NULL) {
    MountUtilsLog("Couldn't read /proc/mounts");
    // TODO(jhermsmeier): Refactor MOUNTUTILS_RESULT into a struct
    // with error_msg, errno, error_code etc. and set the respective
    // values on the struct and move creation of proper errors with
    // the right errno messages etc. into the AsyncWorkers (even better:
    // create a function which creates the proper error from a
    // MOUNTUTILS_RESULT struct).
    // v8::Local<v8::Value> argv[1] = {
    //   Nan::ErrnoException(errno, "setmntent", NULL, "/proc/mounts")
    // };
    // Nan::MakeCallback(Nan::GetCurrentContext()->Global(), callback, 1, argv);
    return MOUNTUTILS_ERROR_GENERAL;
  }

  while ((mnt_p = getmntent_r(proc_mounts, &data, mnt_buf, sizeof(mnt_buf)))) {
    mount_path = mnt_p->mnt_fsname;
    if (strncmp(mount_path, device_path, strlen(device_path)) == 0) {
      MountUtilsLog("Mount point " + std::string(mount_path) +
        " belongs to drive " + std::string(device_path));
      mount_dirs.push_back(std::string(mnt_p->mnt_dir));
    }
  }

  MountUtilsLog("Closing /proc/mounts");
  endmntent(proc_mounts);

  // Use umount2() with the MNT_DETACH flag, which performs a lazy unmount;
  // making the mount point unavailable for new accesses,
  // and only actually unmounting when the mount point ceases to be busy
  // TODO(jhermsmeier): See TODO above
  // v8::Local<v8::Value> argv[1] = {
  //   Nan::ErrnoException(errno, "umount2", NULL, mnt_p->mnt_dir)
  // };
  // v8::Local<v8::Object> ctx = Nan::GetCurrentContext()->Global();
  // Nan::MakeCallback(ctx, callback, 1, argv);

  size_t unmounts = 0;
  MOUNTUTILS_RESULT result_code = MOUNTUTILS_SUCCESS;

  for (std::string mount_dir : mount_dirs) {
    MountUtilsLog("Unmounting " + mount_dir + "...");

    mount_path = mount_dir.c_str();

    if (umount2(mount_path, MNT_EXPIRE) != 0) {
      MountUtilsLog("Unmount MNT_EXPIRE " + mount_dir + ": EAGAIN");
      if (umount2(mount_path, MNT_EXPIRE) != 0) {
        MountUtilsLog("Unmount MNT_EXPIRE " + mount_dir + " failed: " +
          std::string(strerror(errno)));
      } else {
        MountUtilsLog("Unmount " + mount_dir + ": success");
        unmounts++;
        continue;
      }
    } else {
      MountUtilsLog("Unmount " + mount_dir + ": success");
      unmounts++;
      continue;
    }

    if (umount2(mount_path, MNT_DETACH) != 0) {
      MountUtilsLog("Unmount MNT_DETACH " + mount_dir + " failed: " +
        std::string(strerror(errno)));
    } else {
      MountUtilsLog("Unmount " + mount_dir + ": success");
      unmounts++;
      continue;
    }

    if (umount2(mount_path, MNT_FORCE) != 0) {
      MountUtilsLog("Unmount MNT_FORCE " + mount_dir + " failed: " +
        std::string(strerror(errno)));
    } else {
      MountUtilsLog("Unmount " + mount_dir + ": success");
      unmounts++;
      continue;
    }
  }

  return unmounts == mount_dirs.size() ?
    MOUNTUTILS_SUCCESS : MOUNTUTILS_ERROR_GENERAL;
}

// FIXME: This is just a stub copy of `UnmountDisk()`,
// and needs implementation!
MOUNTUTILS_RESULT eject_disk(const char *device) {
  return unmount_disk(device);
}
