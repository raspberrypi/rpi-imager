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
#include <thread>
#include <atomic>

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
 */
template<typename Func>
TimeoutResult runWithTimeout(
    Func&& operation,
    const TimeoutConfig& config = {}
) {
    std::atomic<bool> completed{false};
    std::promise<void> promise;
    auto future = promise.get_future();
    
    std::thread worker([&completed, &promise, op = std::forward<Func>(operation)]() {
        op();
        completed.store(true);
        promise.set_value();
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
 */
template<typename Func, typename ResultType>
TimeoutResult runWithTimeout(
    Func&& operation,
    ResultType& result,
    const TimeoutConfig& config = {}
) {
    return runWithTimeout([&]() {
        result = operation();
    }, config);
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
    constexpr int kWatchdogStallTimeoutMs = 60000;         // Normal stall timeout (60s)
    constexpr int kWatchdogAsyncTimeoutMs = 180000;        // Extended timeout when async pending (180s)
    constexpr int kWatchdogReduceDepthThresholdMs = 30000; // Try reducing queue depth after 30s
    constexpr int kWatchdogRestartThresholdMs = 120000;    // Restart only if drain fails (120s)
    
    // === Ring buffer stall detection ===
    constexpr int kRingBufferStallTimeoutMs = 30000;  // Cumulative wait = stall timeout
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
