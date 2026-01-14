/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include "writeprogresswatchdog.h"
#include "downloadthread.h"
#include <QDateTime>
#include <QDebug>

WriteProgressWatchdog::WriteProgressWatchdog(QObject* parent)
    : QObject(parent)
{
    connect(&_timer, &QTimer::timeout, this, &WriteProgressWatchdog::check);
}

WriteProgressWatchdog::~WriteProgressWatchdog()
{
    stop();
}

void WriteProgressWatchdog::start(DownloadThread* thread)
{
    _thread = thread;
    resetProgressTracking();
    _depthReductionAttempted = false;
    _drainAttempted = false;
    _restartAttempted = false;
    _timer.start(CHECK_INTERVAL_MS);
    
    qDebug() << "WriteProgressWatchdog: Started monitoring";
}

void WriteProgressWatchdog::stop()
{
    _timer.stop();
    _thread = nullptr;
    qDebug() << "WriteProgressWatchdog: Stopped";
}

void WriteProgressWatchdog::resetProgressTracking()
{
    _lastBytesWritten = 0;
    _lastBytesDownloaded = 0;
    _lastBytesVerified = 0;
    _lastPendingWrites = 0;
    _lastProgressTime = QDateTime::currentMSecsSinceEpoch();
}

bool WriteProgressWatchdog::hasProgress()
{
    if (!_thread) return false;
    
    quint64 bytesWritten = _thread->bytesWritten();
    quint64 bytesDownloaded = _thread->dlNow();
    quint64 bytesVerified = _thread->verifyNow();
    int pendingWrites = _thread->pendingAsyncWrites();
    
    bool progress = (bytesWritten > _lastBytesWritten) ||
                    (bytesDownloaded > _lastBytesDownloaded) ||
                    (bytesVerified > _lastBytesVerified) ||
                    (pendingWrites < _lastPendingWrites);  // Completions = progress
    
    if (progress) {
        _lastBytesWritten = bytesWritten;
        _lastBytesDownloaded = bytesDownloaded;
        _lastBytesVerified = bytesVerified;
        _lastPendingWrites = pendingWrites;
        _lastProgressTime = QDateTime::currentMSecsSinceEpoch();
    } else {
        _lastPendingWrites = pendingWrites;  // Track even without progress
    }
    
    return progress;
}

int WriteProgressWatchdog::getEffectiveTimeoutMs()
{
    // Use longer timeout when async writes are pending (slow device draining)
    int pending = _thread ? _thread->pendingAsyncWrites() : 0;
    return (pending > 0) ? ASYNC_TIMEOUT_MS : STALL_TIMEOUT_MS;
}

bool WriteProgressWatchdog::tryPollingRecovery()
{
    if (!_thread) return false;
    
    int beforePoll = _thread->pendingAsyncWrites();
    if (beforePoll == 0) return false;
    
    _thread->forcePollAsyncCompletions();
    int afterPoll = _thread->pendingAsyncWrites();
    
    if (afterPoll < beforePoll) {
        qDebug() << "WriteProgressWatchdog: Polling recovered" 
                 << (beforePoll - afterPoll) << "completions";
        _lastProgressTime = QDateTime::currentMSecsSinceEpoch();
        _lastPendingWrites = afterPoll;
        return true;
    }
    
    return false;
}

bool WriteProgressWatchdog::tryQueueDepthReduction()
{
    if (!_thread) return false;
    
    int currentDepth = _thread->getAsyncQueueDepth();
    if (currentDepth <= 2) {
        return false;  // Already at minimum
    }
    
    int newDepth = qMax(2, currentDepth / 2);
    int pendingWrites = _thread->pendingAsyncWrites();
    
    qDebug() << "WriteProgressWatchdog: Reducing queue depth" << currentDepth 
             << "->" << newDepth << "(" << pendingWrites << "pending)";
    
    if (_thread->reduceAsyncQueueDepth(newDepth)) {
        _depthReductionAttempted = true;
        // Don't reset timer - let normal progress tracking handle drain
        // If pending count decreases, hasProgress() will see it
        return true;
    }
    
    return false;
}

bool WriteProgressWatchdog::tryDrainAndHotSwap()
{
    if (!_thread) return false;
    
    _drainAttempted = true;
    int pendingBefore = _thread->pendingAsyncWrites();
    
    qDebug() << "WriteProgressWatchdog: Attempting drain + hot-swap (" 
             << pendingBefore << "pending writes)";
    
    // This will wait as long as completions keep coming (up to DRAIN_STALL_TIMEOUT_SECONDS between each)
    if (_thread->drainAndSwitchToSync(DRAIN_STALL_TIMEOUT_SECONDS)) {
        // Success! All pending writes completed, now in sync mode
        qDebug() << "WriteProgressWatchdog: Hot-swap successful - continuing in sync mode";
        _lastProgressTime = QDateTime::currentMSecsSinceEpoch();  // Reset stall timer
        
        emit switchedToSyncMode(tr("Switched to compatibility mode - write continuing..."));
        return true;
    }
    
    // Drain timed out - some writes still stuck
    int pendingAfter = _thread->pendingAsyncWrites();
    qDebug() << "WriteProgressWatchdog: Drain failed -" << pendingAfter 
             << "writes still pending (was" << pendingBefore << ")";
    
    return false;
}

void WriteProgressWatchdog::check()
{
    if (!_thread) return;
    
    // Check for progress - includes pending writes decreasing (drain = progress)
    if (hasProgress()) {
        return;  // All good - timer was reset in hasProgress()
    }
    
    // No progress at all (bytes AND pending count unchanged)
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 stallMs = now - _lastProgressTime;
    int timeoutMs = getEffectiveTimeoutMs();
    int pendingWrites = _thread->pendingAsyncWrites();
    
    // Hard timeout - truly stuck
    if (stallMs >= timeoutMs) {
        qDebug() << "WriteProgressWatchdog: HARD TIMEOUT after" << stallMs/1000 << "s"
                 << "- pending:" << pendingWrites;
        stop();
        emit hardTimeout(tr("Write stalled - no progress for %1 seconds.\n\n"
                           "Please check your storage device and try again.")
                        .arg(timeoutMs/1000));
        return;
    }
    
    // Recovery phase 1: Try polling (might unstick IOCP)
    if (pendingWrites > 0 && tryPollingRecovery()) {
        return;  // Polling retrieved completions
    }
    
    // Recovery phase 2: Reduce queue depth (after 30s of no progress)
    // This gives slow devices more time by reducing memory pressure
    if (stallMs >= REDUCE_DEPTH_THRESHOLD_MS && pendingWrites > 0 && !_depthReductionAttempted) {
        tryQueueDepthReduction();
        // Don't return - let normal progress tracking handle drain
    }
    
    // Recovery phase 3: Try drain + hot-swap to sync (after 60s)
    // This waits for pending writes to complete naturally, then switches to sync
    // Much better than restart because we keep all progress!
    if (stallMs >= (REDUCE_DEPTH_THRESHOLD_MS * 2) && pendingWrites > 0 && !_drainAttempted) {
        if (tryDrainAndHotSwap()) {
            return;  // Success - now in sync mode, no restart needed
        }
        // Drain failed - fall through to restart
    }
    
    // Recovery phase 4: Full restart (only if drain failed, after 120s)
    if (stallMs >= RESTART_THRESHOLD_MS && pendingWrites > 0 && !_restartAttempted) {
        _restartAttempted = true;
        qDebug() << "WriteProgressWatchdog: Drain failed, " << pendingWrites 
                 << "writes still stuck - recommending restart";
        stop();
        emit restartNeeded(tr("Storage device not responding. "
                             "Restarting in compatibility mode..."));
    }
    
    // Emit warning for debugging
    if (stallMs >= timeoutMs / 2) {
        emit stallWarning(static_cast<int>(stallMs/1000), pendingWrites);
    }
}
