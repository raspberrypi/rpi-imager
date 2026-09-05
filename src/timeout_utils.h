/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 * 
 * Simple timeout utility for I/O operations that may hang.
 * 
 * This is a low-level utility to unblock syscalls that hang indefinitely.
 * It does NOT handle user-facing warnings or recovery - that's the job of
 * WriteProgressWatchdog at the orchestration layer.
 * 
 * Usage:
 *   auto result = runWithTimeout(
 *       [&]() { return pwrite(fd, data, size, offset); },
 *       writeResult,
 *       TimeoutConfig(30).withOnTimeout([&]() { close(fd); })
 *   );
 */

#ifndef TIMEOUT_UTILS_H_
#define TIMEOUT_UTILS_H_

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <atomic>
#include <type_traits>

namespace rpi_imager {

/**
 * @brief Result of a timeout-protected operation
 */
enum class TimeoutResult {
    Completed,    ///< Operation finished within timeout
    TimedOut,     ///< Timeout exceeded - operation may still be running
    Cancelled     ///< Operation was cancelled externally
};

/**
 * @brief Configuration for timeout-protected operations
 */
struct TimeoutConfig {
    /// Timeout duration (default: 60s)
    std::chrono::seconds timeout = std::chrono::seconds(60);
    
    /// Called when timeout is exceeded (e.g., close fd to unblock operation)
    /// The operation thread will be detached after this returns
    std::function<void()> onTimeout = nullptr;
    
    /// Pointer to external cancellation flag (optional)
    std::atomic<bool>* cancelFlag = nullptr;
    
    /// How often to check for cancellation/completion (default: 100ms)
    std::chrono::milliseconds checkInterval = std::chrono::milliseconds(100);
    
    // Constructors
    TimeoutConfig() = default;
    explicit TimeoutConfig(int timeoutSeconds) 
        : timeout(std::chrono::seconds(timeoutSeconds)) {}
    
    // Builder-style setters
    TimeoutConfig& withTimeout(int seconds) {
        timeout = std::chrono::seconds(seconds);
        return *this;
    }
    TimeoutConfig& withOnTimeout(std::function<void()> callback) {
        onTimeout = std::move(callback);
        return *this;
    }
    TimeoutConfig& withCancelFlag(std::atomic<bool>* flag) {
        cancelFlag = flag;
        return *this;
    }
    
};

/**
 * @brief Run an operation with timeout protection
 * 
 * The operation runs in a separate thread. This function blocks until:
 * - The operation completes (returns Completed)
 * - Timeout is exceeded (returns TimedOut, thread is detached)
 * - External cancellation is requested (returns Cancelled)
 * 
 * @note On TimedOut, the operation thread is detached and may continue running.
 *       Use onTimeout to trigger an abort (e.g., close fd to unblock syscall).
 *
 * @warning THE OPERATION MUST OWN EVERYTHING IT TOUCHES.
 *
 * On the TimedOut and Cancelled paths the worker is detached and this
 * function returns immediately, so the operation keeps running -- for as
 * long as its syscall blocks -- while the caller's frame unwinds and the
 * caller's objects are destroyed. Anything the callable reaches through a
 * reference or a raw pointer is therefore liable to be freed underneath it.
 *
 * Capture by value, or by shared_ptr for anything that has to be written
 * back or is too large to copy. Do not capture locals by reference, and do
 * not hand it a raw pointer to something the caller owns.
 *
 * This is not hypothetical. DownloadThread's end-of-device write passed a
 * pointer into a local buffer and a raw pointer to a member-owned file
 * object; cancelling early in a write produced a heap-use-after-free --
 * AddressSanitizer caught a 1 MiB read on the detached worker thread -- and
 * a SEGFAULT in the suite. FileOperations' sync fallback captured its
 * result variables by reference and stored into a frame that had returned.
 * Both are fixed; the pattern is easy to reintroduce.
 */
template<typename Func>
TimeoutResult runWithTimeout(
    Func&& operation,
    const TimeoutConfig& config = {}
) {
    // The shared state lives on the heap and the worker holds a strong
    // reference to it.
    //
    // It used to be two locals captured by reference. On the timeout and
    // cancellation paths below the worker is detached and this function
    // returns, so those references pointed into a frame that no longer
    // existed; when the abandoned operation finally unblocked it stored
    // through them and called set_value() on a destroyed promise. Under ASan
    // that is a stack-use-after-return, under TSan a heap-use-after-free, and
    // uninstrumented it is a segfault -- reachable in the product by
    // cancelling a write while the device is still being prepared, since
    // _openAndPrepareDevice() runs through here.
    //
    // Keeping the state alive through a shared_ptr means a detached worker
    // writes somewhere that is still valid, and the memory is released when
    // the last of the two lets go of it.
    struct SharedState {
        std::atomic<bool> completed{false};
        std::promise<void> promise;
    };
    auto state = std::make_shared<SharedState>();
    auto future = state->promise.get_future();

    std::thread worker([state, op = std::forward<Func>(operation)]() mutable {
        op();
        state->completed.store(true);
        state->promise.set_value();
    });
    
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        // Check for external cancellation
        if (config.cancelFlag && config.cancelFlag->load()) {
            worker.detach();
            return TimeoutResult::Cancelled;
        }
        
        // Check if operation completed
        if (future.wait_for(config.checkInterval) == std::future_status::ready) {
            worker.join();
            return TimeoutResult::Completed;
        }
        
        // Check for timeout
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed >= config.timeout) {
            if (config.onTimeout) {
                config.onTimeout();
            }
            worker.detach();
            return TimeoutResult::TimedOut;
        }
    }
}

/**
 * @brief Run an operation with timeout, capturing the return value
 * 
 * @note This overload is only enabled when Func returns a non-void type
 *       that can be assigned to ResultType. This prevents ambiguity with
 *       the void-returning overload above.
 */
template<typename Func, typename ResultType,
         typename = typename std::enable_if<!std::is_void<
             decltype(std::declval<Func>()())>::value>::type>
TimeoutResult runWithTimeout(
    Func&& operation,
    ResultType& result,
    const TimeoutConfig& config = {}
) {
    // Same hazard as the void overload, one level up: `result` is the
    // caller's variable, and a detached worker assigning to it after this
    // function has returned writes into a dead frame. The worker fills a
    // heap-allocated slot instead, and the value is copied out only when the
    // operation actually completed -- so a timed-out or cancelled operation
    // that finishes later cannot scribble over the caller's result either.
    //
    // The operation itself needs the same treatment. Capturing it by
    // reference left the detached worker calling through a reference to the
    // caller's lambda, which is usually a temporary that dies at the end of
    // the full expression this function was called in. The void overload
    // above already moves the callable into the worker; this one has to hand
    // it over the same way, or a cancelled write segfaults once the
    // abandoned operation unblocks.
    auto slot = std::make_shared<ResultType>(result);

    const TimeoutResult outcome = runWithTimeout(
        [slot, op = std::forward<Func>(operation)]() mutable {
            *slot = op();
        }, config);

    if (outcome == TimeoutResult::Completed)
        result = *slot;

    return outcome;
}

/**
 * @brief Centralized timeout constants for the recovery system
 * 
 * All timeout values used for stall detection and recovery should be defined here
 * to ensure consistency across components. Components should use these values
 * rather than defining their own magic numbers.
 */
namespace TimeoutDefaults {
    // === Sync fallback timeouts (used when async I/O fails) ===
    constexpr int kSyncWriteTimeoutSeconds = 30;  // Per-write timeout in sync fallback mode
    constexpr int kSyncFsyncTimeoutSeconds = 60;  // fsync timeout (longer for buffer flush)
    
    // === Async I/O timeouts ===
    constexpr int kAsyncQueueWaitTimeoutSeconds = 30;    // Max time to wait for async queue slot
    constexpr int kAsyncDrainStallTimeoutSeconds = 30;   // Per-completion timeout during drain
    constexpr int kAsyncFirstCompletionTimeoutMs = 5000; // Max wait for first IOCP completion
    
    // === Progress watchdog thresholds ===
    constexpr int kWatchdogCheckIntervalMs = 1000;         // How often watchdog checks progress
    constexpr int kWatchdogStallTimeoutMs = 180000;        // Normal stall timeout (180s, matches async timeout for slow cards)
    constexpr int kWatchdogAsyncTimeoutMs = 180000;        // Extended timeout when async pending (180s)
    constexpr int kWatchdogReduceDepthThresholdMs = 30000; // Try reducing queue depth after 30s
    constexpr int kWatchdogRestartThresholdMs = 120000;    // Restart only if drain fails (120s)
    
    // === Ring buffer stall detection ===
    // Cumulative wait with nothing moving = stall timeout.
    //
    // This has to clear the longest a working pipeline can legitimately sit
    // still, because tripping it aborts the write. The longest such pause is
    // one fastboot sparse segment: the consumer holds its ring slot across the
    // whole 256 MB download and the device's commit of it, and the producer
    // backs up behind that -- tens of seconds on a slow link, which is why 30s
    // was too tight to enable. The disk side is the same shape; the app's own
    // tolerance for a slow card is 180s (see kWatchdogStallTimeoutMs), and it
    // starts intervening at 30s rather than giving up.
    //
    // 90s sits above anything a working device does and below the watchdog, so
    // a genuinely dead pipeline is diagnosed specifically -- "the download has
    // stalled", naming the network or the disk -- rather than surfacing later
    // as a generic hard timeout.
    constexpr int kRingBufferStallTimeoutMs = 90000;
    constexpr int kRingBufferStallEventThresholdMs = 50; // Minimum stall to record as event
    
    // === Adaptive recovery thresholds ===
    constexpr int kHighLatencyThresholdMs = 10000;  // Per-write latency triggering depth reduction
    constexpr int kSlowProgressThresholdSeconds = 10; // Seconds without progress before reducing depth
    constexpr int kMinAsyncQueueDepth = 2;          // Minimum queue depth during recovery
    
    // === Memory-based recovery ===
    constexpr int kMemoryCheckIntervalMs = 2000;    // How often to check available memory
    constexpr int kCriticalMemoryMB = 256;          // Below this, reduce queue depth
    
    // === Device preparation timeouts ===
    constexpr int kHardTimeoutSeconds = 120;  // Timeout for BLKDISCARD, end-of-device writes
}

} // namespace rpi_imager

#endif // TIMEOUT_UTILS_H_
