/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#ifndef WRITEPROGRESSWATCHDOG_H
#define WRITEPROGRESSWATCHDOG_H

#include <QObject>
#include <QTimer>
#include "timeout_utils.h"

class DownloadThread;

/**
 * WriteProgressWatchdog - Monitors write progress and detects stalls
 * 
 * This component watches a DownloadThread and emits signals when:
 * - A recoverable stall is detected (should restart in sync mode)
 * - An unrecoverable timeout occurs (should abort with error)
 * 
 * == RECOVERY OWNERSHIP HIERARCHY ==
 * 
 * The timeout/recovery system has multiple layers. This documents who owns what:
 * 
 * 1. WriteProgressWatchdog (this class) - ORCHESTRATION LAYER
 *    - Owned by: ImageWriter
 *    - Monitors: Overall progress (bytes written, downloaded, verified, pending count)
 *    - Timeouts: 60s normal, 180s with async pending, 120s before restart
 *    - Actions: Emit signals for UI warning, hot-swap to sync, restart, hard timeout
 *    - Authority: Decides WHEN to attempt recovery, delegates HOW to lower layers
 * 
 * 2. RingBuffer - DATA FLOW LAYER
 *    - Owned by: DownloadExtractThread
 *    - Monitors: Producer/consumer stalls in ring buffer acquisition
 *    - Timeouts: 30s cumulative wait = stall timeout (see timeout_utils.h)
 *    - Actions: Sets stallTimeoutExceeded flag, returns nullptr from acquire
 *    - Authority: Detects pipeline deadlocks (download stalled, write stalled)
 * 
 * 3. FileOperations (platform-specific) - I/O LAYER
 *    - Owned by: DownloadThread
 *    - Monitors: Individual async I/O completion latency, queue drain progress
 *    - Timeouts: 30s per-write in sync fallback, 5s for first IOCP completion
 *    - Actions: Adaptive queue depth reduction, sync fallback replay
 *    - Authority: Handles platform-specific I/O recovery, reports status up
 * 
 * 4. timeout_utils.h - SYSCALL LAYER
 *    - Used by: FileOperations, DownloadThread
 *    - Monitors: Individual blocking syscalls (pwrite, fsync, ioctl)
 *    - Timeouts: Configurable per-call (typically 30-60s)
 *    - Actions: Detach thread, invoke onTimeout callback (e.g., close fd)
 *    - Authority: Unblocks truly stuck syscalls, lowest level escape hatch
 * 
 * FLOW: WriteProgressWatchdog detects no overall progress → tries recovery actions
 *       in order (poll, reduce depth, drain+hot-swap, restart) → if all fail,
 *       emits hardTimeout → ImageWriter shows error to user.
 * 
 * Usage:
 *   _watchdog = new WriteProgressWatchdog(this);
 *   connect(_watchdog, &WriteProgressWatchdog::restartNeeded, this, &MyClass::handleRestart);
 *   connect(_watchdog, &WriteProgressWatchdog::hardTimeout, this, &MyClass::handleError);
 *   _watchdog->start(thread);
 */
class WriteProgressWatchdog : public QObject
{
    Q_OBJECT

public:
    explicit WriteProgressWatchdog(QObject* parent = nullptr);
    ~WriteProgressWatchdog();
    
    /// Start monitoring the given thread
    void start(DownloadThread* thread);
    
    /// Stop monitoring
    void stop();
    
    /// Check if currently monitoring
    bool isRunning() const { return _timer.isActive(); }

signals:
    /// Emitted when hot-swap to sync mode succeeded (no restart needed)
    void switchedToSyncMode(QString reason);
    
    /// Emitted when hot-swap failed and full restart is needed
    void restartNeeded(QString reason);
    
    /// Emitted when an unrecoverable timeout occurs - caller should abort with error
    void hardTimeout(QString errorMessage);
    
    /// Emitted as a warning before timeout (for logging/UI feedback)
    void stallWarning(int secondsStalled, int pendingWrites);

private slots:
    void check();

private:
    // Configuration - uses centralized constants from timeout_utils.h
    static constexpr int CHECK_INTERVAL_MS = rpi_imager::TimeoutDefaults::kWatchdogCheckIntervalMs;
    static constexpr int STALL_TIMEOUT_MS = rpi_imager::TimeoutDefaults::kWatchdogStallTimeoutMs;
    static constexpr int ASYNC_TIMEOUT_MS = rpi_imager::TimeoutDefaults::kWatchdogAsyncTimeoutMs;
    static constexpr int REDUCE_DEPTH_THRESHOLD_MS = rpi_imager::TimeoutDefaults::kWatchdogReduceDepthThresholdMs;
    static constexpr int DRAIN_STALL_TIMEOUT_SECONDS = rpi_imager::TimeoutDefaults::kAsyncDrainStallTimeoutSeconds;
    static constexpr int RESTART_THRESHOLD_MS = rpi_imager::TimeoutDefaults::kWatchdogRestartThresholdMs;
    
    // State
    DownloadThread* _thread = nullptr;
    QTimer _timer;
    
    // Progress tracking
    quint64 _lastBytesWritten = 0;
    quint64 _lastBytesDownloaded = 0;
    quint64 _lastBytesVerified = 0;
    int _lastPendingWrites = 0;
    qint64 _lastProgressTime = 0;
    
    // Recovery state
    bool _depthReductionAttempted = false;
    bool _drainAttempted = false;
    bool _restartAttempted = false;
    
    // Helper methods
    bool hasProgress();
    void resetProgressTracking();
    int getEffectiveTimeoutMs();
    bool tryPollingRecovery();
    bool tryQueueDepthReduction();
    bool tryDrainAndHotSwap();
};

#endif // WRITEPROGRESSWATCHDOG_H
