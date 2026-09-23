/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_WINDOWS_H_
#define FILE_OPERATIONS_WINDOWS_H_

#include "../file_operations.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace rpi_imager {

// Windows implementation using Win32 API with IOCP-based async I/O
class WindowsFileOperations : public FileOperations {
 public:
  WindowsFileOperations();
  ~WindowsFileOperations() override;

  // Non-copyable, non-movable (due to IOCP resources)
  WindowsFileOperations(const WindowsFileOperations&) = delete;
  WindowsFileOperations& operator=(const WindowsFileOperations&) = delete;
  WindowsFileOperations(WindowsFileOperations&&) = delete;
  WindowsFileOperations& operator=(WindowsFileOperations&&) = delete;

  FileError OpenDevice(const std::string& path) override;
  FileError CreateTestFile(const std::string& path, std::uint64_t size) override;
  FileError WriteAtOffset(
      std::uint64_t offset,
      const std::uint8_t* data,
      std::size_t size) override;
  FileError GetSize(std::uint64_t& size) override;
  FileError Close() override;
  bool IsOpen() const override;

  // Streaming I/O operations
  FileError WriteSequential(const std::uint8_t* data, std::size_t size) override;
  FileError ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) override;
  
  // File positioning
  FileError Seek(std::uint64_t position) override;
  std::uint64_t Tell() const override;
  
  // Sync operations
  FileError ForceSync() override;
  FileError Flush() override;
  
  // Sequential read optimization
  void PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) override;
  
  // Handle access (Windows uses HANDLE, so we return a cast to int)
  int GetHandle() const override;

  // Get the last Windows error code
  int GetLastErrorCode() const override;

  // Classify the last write error, including a Windows Defender Controlled
  // Folder Access probe when access is denied.
  WriteErrorClass ClassifyLastWriteError() const override;

  // Check if direct I/O is enabled
  bool IsDirectIOEnabled() const override { return using_direct_io_; }
  
  // Enable or disable direct I/O
  FileError SetDirectIOEnabled(bool enabled) override;
  
  // Get direct I/O attempt details
  DirectIOInfo GetDirectIOInfo() const override { 
      DirectIOInfo info = direct_io_info_;
      info.currently_enabled = using_direct_io_;
      return info;
  }
  
  // ============= Async I/O API (Windows: using IOCP) =============
  bool SetAsyncQueueDepth(int depth) override;
  int GetAsyncQueueDepth() const override { return async_queue_depth_; }
  bool IsAsyncIOSupported() const override { return true; }
  FileError AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                  AsyncWriteCallback callback = nullptr) override;
  int GetPendingWriteCount() const override { return pending_writes_.load(); }
  void PollAsyncCompletions() override;
  FileError WaitForPendingWrites() override;
  void CancelAsyncIO() override;
  void DiagnoseStuckWrites();  // Debug helper to check actual I/O state
  std::vector<PendingWriteInfo> GetPendingWritesSorted() const override;
  void ReduceQueueDepthForRecovery(int newDepth) override;
  // GetAsyncIOStats() inherited from FileOperations base class

 private:
  HANDLE handle_;
  std::string current_path_;
  int last_error_code_;
  bool using_direct_io_;
  DirectIOInfo direct_io_info_;
  // Share mode the device was opened with. Physical drives are opened with
  // exclusive write access (FILE_SHARE_READ only) so the OS cannot mount the
  // partition mid-write; remembered here so reopens (e.g. toggling direct I/O)
  // preserve exclusivity. See OpenDevice().
  DWORD current_share_mode_ = FILE_SHARE_READ | FILE_SHARE_WRITE;
  
  // IOCP async I/O state
  int async_queue_depth_;
  std::atomic<int> pending_writes_;
  std::atomic<bool> cancelled_;  // Flag to cancel pending async I/O
  FileError first_async_error_;
  // Where the next sequential write goes. Both write paths read and advance
  // this one counter, because either can follow the other: an async write that
  // fails is retried synchronously, and the synchronous write has to land where
  // the async queue had got to. It used to take its offset from
  // current_file_position_, which only reads and Seek() ever moved, so a
  // fallback after any async write rewound to the last seek and overwrote what
  // had already been written.
  std::uint64_t write_offset_;
  std::uint64_t current_file_position_;  // Track position for overlapped sync reads
  HANDLE iocp_;  // I/O Completion Port handle
  
  // Extended OVERLAPPED structure to track per-write context
  struct AsyncWriteContext {
    OVERLAPPED overlapped;
    AsyncWriteCallback callback;
    const std::uint8_t* data;  // For sync fallback replay
    std::size_t size;
    WindowsFileOperations* self;
    std::chrono::steady_clock::time_point submit_time;  // For latency tracking
    int retries = 0;  // Transient completion failures reissued so far
  };
  
  mutable std::mutex pending_mutex_;
  std::unordered_map<OVERLAPPED*, AsyncWriteContext*> pending_contexts_;
  
  // Note: write_latency_stats_ is inherited from FileOperations base class
  
  // Record the Win32 error behind a write failure so ClassifyLastWriteError()
  // can name it. Async completions arrive after the failing call has returned
  // and later writes may still succeed, so the first error is kept rather than
  // the most recent one.
  void LatchWriteError(DWORD error);

  FileError LockVolume(const std::string& path);
  FileError UnlockVolume();
  FileError OpenInternal(const std::string& path, DWORD access, DWORD creation, DWORD flags = FILE_ATTRIBUTE_NORMAL, DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE);
  
  static bool IsPhysicalDrivePath(const std::string& path);

  // Only a physical drive can be transiently held; a file cannot.
  bool OpenFailureMayBeTransient(const std::string& path) const override
  {
    if (!IsPhysicalDrivePath(path))
      return false;
    const int err = last_error_code_;
    return err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION ||
           err == ERROR_NOT_READY;
  }
  
#ifdef FILEOPS_ENABLE_TEST_API
 public:
  // Make the next `count` write completions come back as failures carrying
  // `error`, whatever the device actually did.
  //
  // The reissue path is otherwise unreachable from a test. It answers the
  // device refusing writes while Windows re-enumerates it after a partition
  // table rewrite, and no scratch file or attached VHD can be persuaded to
  // answer ERROR_NOT_READY on demand. Injecting at the completion leaves the
  // rest genuine: the reissued write goes to the real device.
  void FailNextWriteCompletions(unsigned long error, int count) {
    injected_write_error_ = error;
    injected_write_failures_.store(count);
  }

  int RemainingInjectedWriteFailures() const { return injected_write_failures_.load(); }

  // Replay the pending writes synchronously, as the emergency path does.
  //
  // Its only caller is a five-minute timeout inside WaitForPendingWrites,
  // reached when writes stop completing altogether. No case can wait that
  // long and none can stop a scratch file completing, so the replay -- which
  // must put every outstanding buffer back at the offset it was given -- had
  // no cover at all.
  FileError ReplayPendingWritesSynchronously() { return AttemptSyncFallback(); }

 private:
  unsigned long injected_write_error_ = 0;
  std::atomic<int> injected_write_failures_{0};
#endif

  // Turn a completion into the injected failure, if one is armed. Compiled
  // away entirely unless the test API is enabled.
  void ApplyWriteFaultInjection(BOOL& success, DWORD& error) {
#ifdef FILEOPS_ENABLE_TEST_API
    if (success && injected_write_failures_.load() > 0) {
      injected_write_failures_.fetch_sub(1);
      success = FALSE;
      error = static_cast<DWORD>(injected_write_error_);
    }
#else
    (void)success;
    (void)error;
#endif
  }

  bool InitIOCP();
  void CleanupIOCP();
  void ProcessCompletions(bool wait);

  // Put a write that came back with a transient error onto the wire again, at
  // the offset it was already assigned. Returns true if it is in flight once
  // more, in which case it keeps its slot in pending_writes_ and its callback
  // has not run -- so the buffer it points at is still owned by us. Returns
  // false if the error was not transient, the attempts are used up or the write
  // was cancelled, and the caller reports the failure as it always did.
  bool ReissueAfterTransientFailure(AsyncWriteContext* ctx, DWORD error);
  FileError AttemptSyncFallback() override;
  bool DrainAndSwitchToSync(int timeoutSeconds) override;
  
  // Wait for overlapped I/O with cancellation support
  // Returns true if completed successfully, false if cancelled or error
  bool WaitForOverlappedWithCancel(OVERLAPPED* overlapped, DWORD* bytes_transferred);
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_WINDOWS_H_ 