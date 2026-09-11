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

    /// How long to wait for the worker to finish before abandoning it, on
    /// the timeout and cancellation paths (default: 2s).
    ///
    /// Most operations reaching those paths are not wedged at all -- a
    /// cancellation usually arrives while a perfectly healthy write is in
    /// flight, and it finishes in milliseconds. Waiting briefly lets the
    /// thread be joined and everything it touched freed safely, instead of
    /// being abandoned to run on against the caller's dying frame.
    std::chrono::milliseconds joinGrace = std::chrono::milliseconds(2000);
    
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
    TimeoutConfig& withJoinGrace(std::chrono::milliseconds grace) {
        joinGrace = grace;
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
 * @warning THE OPERATION MUST OWN EVERYTHING IT TOUCHES.
 */
template<typename Func>
TimeoutResult runWithTimeout(
    Func&& operation,
    const TimeoutConfig& config = {}
) {
    // The shared state lives on the heap and the worker holds a strong
    // reference to it.
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
    
    // Give up on the worker, but try to take it with us first.
    auto stopWaiting = [&worker, &future, &config](TimeoutResult outcome) {
        if (future.wait_for(config.joinGrace) == std::future_status::ready) {
            worker.join();
        } else {
            worker.detach();
        }
        return outcome;
    };

    while (true) {
        // Check for external cancellation
        if (config.cancelFlag && config.cancelFlag->load()) {
            return stopWaiting(TimeoutResult::Cancelled);
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
            return stopWaiting(TimeoutResult::TimedOut);
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
