/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_freebsd.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <sys/sysctl.h>
#include <aio.h>
#include <errno.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <functional>
#include "../timeout_utils.h"

#include <camlib.h>
#include <libgeom.h>

using rpi_imager::TimeoutResult;
using rpi_imager::TimeoutConfig;
using rpi_imager::runWithTimeout;
using rpi_imager::TimeoutDefaults::kSyncWriteTimeoutSeconds;
using rpi_imager::TimeoutDefaults::kSyncFsyncTimeoutSeconds;
using rpi_imager::TimeoutDefaults::kMinAsyncQueueDepth;
using rpi_imager::TimeoutDefaults::kHighLatencyThresholdMs;
using rpi_imager::TimeoutDefaults::kAsyncFirstCompletionTimeoutMs;

#include <fstream>

namespace rpi_imager {

// Forward declaration — defined at bottom of file, called from OpenDevice()
FileOperations::DeviceIOLimits QueryPlatformDeviceIOLimits(const std::string& path);

// Use the common logging function from file_operations.cpp
static void Log(const std::string& msg) {
    FileOperationsLog(msg);
}

FreeBSDFileOperations::FreeBSDFileOperations()
    : UnixFileOperations(), is_destr_(0) {
}

FreeBSDFileOperations::~FreeBSDFileOperations() {
  is_destr_ = 1;
  Close();
}

// Block device detection using libgeom
bool FreeBSDFileOperations::IsBlockDevicePath(const std::string& path) {
    struct gmesh devtree;
    struct gclass *geom_class;
    struct ggeom *disk;

    int error = geom_gettree(&devtree);
    if (error != 0) {
        std::cerr << "FreeBSDFileOperations::IsBlockDevicePath: Failed to open GEOM device tree"
                  << ", &devtree=" << &devtree
                  << ", errno=" << error << " (" << std::strerror(error) << ")"
                  << std::endl;
        return false;
    }

    LIST_FOREACH(geom_class, &devtree.lg_class, lg_class) {
        if (std::string(geom_class->lg_name) != "DISK") {
            continue;
        }

        LIST_FOREACH(disk, &geom_class->lg_geom, lg_geom) {
            if (std::string(g_device_path(disk->lg_name)) == std::string(g_device_path(path.c_str()))) {
                return true;
            }
        }

        break;
    }

    return false;
}

// Device size query using DIOCGMEDIASIZE ioctl
FileError FreeBSDFileOperations::GetDeviceSize(std::uint64_t& size) {
    if (ioctl(fd_, DIOCGMEDIASIZE, &size) == -1) {
        std::cerr << "FreeBSDFileOperations::GetDeviceSize: "
                  << "Failed to get device size"
                  << ", errno=" << errno << " (" << std::strerror(errno) << ")"
                  << std::endl;
        last_error_code_ = errno;
        return FileError::kSizeError;
    }
    return FileError::kSuccess;
}

// Device I/O limits query using camlib
FileOperations::DeviceIOLimits QueryPlatformDeviceIOLimits(const std::string& path) {
    FileOperations::DeviceIOLimits limits;
    struct cam_device *dev;
    union ccb *ccb;
    int val;
    size_t len = sizeof(val);

    // camcontrol has soft queue depth
    dev = cam_open_device(path.c_str(), O_RDWR);
    if (dev == NULL)
        return limits;

    ccb = cam_getccb(dev);
    if (ccb == NULL)
        return limits;

    CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cgds);

	ccb->ccb_h.func_code = XPT_GDEV_STATS;
	if (cam_send_ccb(dev, ccb) < 0)
		goto bail;

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
		goto bail;

    limits.suggested_queue_depth = ccb->cgds.dev_openings +
        ccb->cgds.dev_active;

    // Get max single I/O
    if(sysctlbyname("kern.maxphys", &val, &len, nullptr, 0) == -1) {
        goto bail;
    }

    if (val > 0)
        limits.max_transfer_bytes = static_cast<size_t>(val);

bail:
    cam_freeccb(ccb);
    return limits;
}

// All functions below concern themselves with aio
bool FreeBSDFileOperations::SetAsyncQueueDepth(int depth) {
    int old, nlen = sizeof(depth);
    size_t olen = sizeof(old);

    // Since we're doing raw disk I/O, only set values for that pool
    if (sysctlbyname("vfs.aio.max_buf_aio", &old, &olen,
        &depth, nlen) == -1)
        return false;
    async_queue_depth_ = depth;
    return true;
}

void FreeBSDFileOperations::ProcessCompletions(bool wait) {
    if (pending_writes_.load() == 0) {
        return;
    }

    ssize_t result;
    struct aiocb *iocb{};
    struct timespec timeout{};

    if (wait && !cancelled_.load()) {
        // Use timeout-based wait so we can check for cancellation
        // Also add overall timeout to prevent infinite waiting if device stops responding
        timeout.tv_nsec = 100000000; // 100ms
        auto waitStart = std::chrono::steady_clock::now();

recheck:
        // We don't wait unless at least one write has been queued,
        // so EAGAIN shouldn't be possible.
        if ((result = aio_waitcomplete(&iocb, &timeout)) == -1 &&
            errno != EINPROGRESS && iocb == nullptr) {
            std::ostringstream oss;
            oss << "aio_waitcomplete: Couldn't fetch an event: " << strerror(errno)
                << " - trying again.";
            Log(oss.str());
        }

        // If timeout, try again with overall limit
        if (iocb == nullptr && !cancelled_.load() && pending_writes_.load() > 0) {
            auto elapsed = std::chrono::steady_clock::now() - waitStart;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= kAsyncFirstCompletionTimeoutMs) {
                Log("ProcessCompletions: No completion received in " + std::to_string(kAsyncFirstCompletionTimeoutMs) +
                    "ms, returning to allow recovery");
                return;  // Return to caller so queue-wait timeout can trigger
            }
            goto recheck;
        }
        timeout.tv_nsec = 0;
    } else {
        result = aio_waitcomplete(&iocb, &timeout);
    }

    while(true) {
        // This must be an error with aio_waitcomplete since
        // an error with aio_write should return a valid aiocb.
        if (iocb == nullptr) {
            if (errno == EINPROGRESS || errno == EAGAIN)
                break;
            std::ostringstream oss;
            oss << "aio_waitcomplete: Couldn't fetch an event: " << strerror(errno);
            Log(oss.str());
            return;
        }

        auto write_id = (std::uint64_t)iocb->aio_sigevent.sigev_value.sival_ptr;
        std::free(iocb);
        iocb = nullptr;

        AsyncWriteCallback callback = nullptr;
        std::size_t expected_size = 0;
        bool found_in_map = false;
        std::chrono::steady_clock::time_point submit_time;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            auto it = pending_callbacks_.find(write_id);
            if (it != pending_callbacks_.end()) {
                callback = it->second.callback;
                expected_size = it->second.size;
                submit_time = it->second.submit_time;
                pending_callbacks_.erase(it);
                found_in_map = true;
            }
        }

        // Orphaned completion: entry was already consumed (e.g. by sync fallback
        // clearing the map). Just consume and move on.
        if (!found_in_map) {
            Log("aio: completion for unknown write_id " + std::to_string(write_id) +
                " (result=" + std::to_string(result) + ") - ignoring orphaned completion");
            continue;
        }

        // Record write latency (submit to completion) - uses base class's thread-safe stats
        auto completionTime = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(completionTime - submit_time).count();
        write_latency_stats_.recordCompletion(submit_time);

        // Adaptive recovery: if individual write latency is very high, reduce queue depth
        // This helps the system recover when conditions change (memory pressure, slow device)
        constexpr int kMinQueueDepthForReduction = kMinAsyncQueueDepth * 2;  // Trigger reduction above 2x minimum

        int currentPending = pending_writes_.load();

        // Only reduce if we've drained to the current depth (reached equilibrium)
        // This prevents rapid successive reductions before the system can stabilize
        if (latency > kHighLatencyThresholdMs &&
            async_queue_depth_ >= kMinQueueDepthForReduction &&
            currentPending <= async_queue_depth_ &&  // Must be at equilibrium first
            !sync_fallback_mode_) {
          int newDepth = async_queue_depth_ / 2;
          Log("High write latency detected (" + std::to_string(latency) + "ms) - reducing queue depth to " + std::to_string(newDepth));
          ReduceQueueDepthForRecovery(newDepth);
        }

        FileError error = FileError::kWriteError;
        if (result == -1) {
            if (first_async_error_ == FileError::kSuccess) {
                first_async_error_ = error;
            }
            std::ostringstream oss;
            oss << "aio_write failed with error: " << strerror(errno);
            Log(oss.str());
        } else if (static_cast<std::size_t>(result) != expected_size) {
            if (first_async_error_ == FileError::kSuccess) {
                first_async_error_ = error;
            }
            std::ostringstream oss;
            oss << "aio_write short write: expected " << expected_size << ", got " << result;
            Log(oss.str());
        } else {
            error = FileError::kSuccess;
        }

        if (callback) {
            callback(error, error == FileError::kSuccess ? expected_size : 0);
        }

        pending_writes_.fetch_sub(1);
        result = aio_waitcomplete(&iocb, &timeout);
    }
}

FileError FreeBSDFileOperations::AsyncWriteSequential(const std::uint8_t* data, std::size_t size,
                                                      AsyncWriteCallback callback) {
    if (fd_ < 0) {
        if (callback) callback(FileError::kOpenError, 0);
        return FileError::kOpenError;
    }

    if(async_queue_depth_ <= 1 || sync_fallback_mode_) {
        FileError result = WriteSequential(data, size);
        if (callback) callback(result, result == FileError::kSuccess ? size : 0);
        return result;
    }

    if (first_async_error_ != FileError::kSuccess) {
        if (callback) callback(first_async_error_, 0);
        return first_async_error_;
    }

    ProcessCompletions(false);

    while (pending_writes_.load() >= async_queue_depth_) {
        if (cancelled_.load()) {
            if (callback) callback(FileError::kCancelled, 0);
            return FileError::kCancelled;
        }
        ProcessCompletions(true);
    }

    std::uint64_t write_offset = async_write_offset_;
    async_write_offset_ += size;

    std::uint64_t write_id = next_write_id_++;
    auto submit_time = std::chrono::steady_clock::now();
    write_latency_stats_.recordSubmit();

    // Store callback and info for later (includes data/offset for sync fallback)
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_callbacks_[write_id] = PendingWrite{callback, data, write_offset, size, submit_time};
    }

    pending_writes_.fetch_add(1);

    // Set up aiocb for the write
    auto *iocb = static_cast<struct aiocb *>(std::calloc(1, sizeof(struct aiocb)));
    iocb->aio_fildes = fd_;
    iocb->aio_buf = const_cast<std::uint8_t*>(data);
    iocb->aio_nbytes = static_cast<size_t>(size);
    iocb->aio_offset = static_cast<off_t>(write_offset);

    // Set up the sigevent with our write_id
    struct sigevent sigev{};
    sigev.sigev_notify = SIGEV_NONE;
    sigev.sigev_value.sival_ptr = (void *)write_id;
    iocb->aio_sigevent = sigev;

    // Attempt to queue it
    if (aio_write(iocb) == -1) {
        pending_writes_.fetch_sub(1);
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_callbacks_.erase(write_id);
        }
        std::ostringstream oss;
        oss << "aio_write failed: " << strerror(errno);
        Log(oss.str());
        if (callback) callback(FileError::kWriteError, 0);
        std::free(iocb);
        return FileError::kWriteError;
    }

    return FileError::kSuccess;
}

void FreeBSDFileOperations::CancelAsyncIO() {
  cancelled_.store(true);
}

// DESIGN:
// Since aio_cancel doesn't operate on raw disk I/O,
// we can only force future writes to sync. Dealing with pending_writes_
// and callbacks must be deferred to when the destructor is called.
// Another possible solution is to copy the data
// buffer which is passed in the aiocb, but this comes with a
// heavy performance penalty. Both solutions still require
// polling to free the aiocb structures.
FileError FreeBSDFileOperations::AttemptSyncFallback() {
  auto pendingWrites = GetPendingWritesSorted();

  if (pendingWrites.empty()) {
    Log("Sync fallback: no pending writes to replay");
    sync_fallback_mode_ = true;
    return FileError::kSuccess;
  }

  Log("Sync fallback: replaying " + std::to_string(pendingWrites.size()) + " writes synchronously");

  // Cancel any remaining async operations
  cancelled_.store(true);

  // Switch to sync mode for all future writes
  sync_fallback_mode_ = true;

  if (is_destr_) {
    // Clear the pending callbacks (we'll handle them synchronously)
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_callbacks_.clear();
    }
    pending_writes_.store(0);
  }

  // Replay pending writes synchronously with timeout protection
  for (const auto& pw : pendingWrites) {
    ssize_t written = -1;

    auto result = runWithTimeout(
        [this, &pw, &written]() {
          written = pwrite(fd_, pw.data, pw.size, static_cast<off_t>(pw.offset));
        },
        TimeoutConfig(kSyncWriteTimeoutSeconds)
    );

    if (result == TimeoutResult::TimedOut) {
      Log("Sync fallback: write timed out at offset " + std::to_string(pw.offset));
      return FileError::kTimeout;
    }

    if (written < 0 || static_cast<std::size_t>(written) != pw.size) {
      Log("Sync fallback: write failed at offset " + std::to_string(pw.offset));
      return FileError::kWriteError;
    }

    if (is_destr_ && pw.callback) {
      pw.callback(FileError::kSuccess, pw.size);
    }
  }

  if (fd_ < 0) {
    Log("Sync fallback: fd was closed - device unresponsive");
    return FileError::kTimeout;
  }

  // Sync to device with timeout protection
  int syncResult = -1;
  auto fsyncResult = runWithTimeout(
      [this, &syncResult]() { syncResult = fsync(fd_); },
      TimeoutConfig(kSyncFsyncTimeoutSeconds)
  );

  if (fsyncResult == TimeoutResult::TimedOut) {
    Log("Sync fallback: fsync timed out");
    return FileError::kTimeout;
  }

  if (syncResult != 0) {
    Log("Sync fallback: fsync failed");
    return FileError::kSyncError;
  }

  // Update async_write_offset_ to reflect completed writes
  if (!pendingWrites.empty()) {
    const auto& lastWrite = pendingWrites.back();
    async_write_offset_ = lastWrite.offset + lastWrite.size;
  }

  // Reset cancelled flag so future operations can proceed (in sync mode)
  cancelled_.store(false);
  first_async_error_ = FileError::kSuccess;

  Log("Sync fallback successful - continuing in sync mode");
  return FileError::kSuccess;
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<FreeBSDFileOperations>();
}

} // namespace rpi_imager
