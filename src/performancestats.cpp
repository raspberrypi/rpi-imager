/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "performancestats.h"
#include <QFile>
#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <cmath>

PerformanceStats::PerformanceStats(QObject *parent)
    : QObject(parent)
    , _sessionActive(false)
    , _imageSize(0)
    , _sessionStartTime(0)
    , _sessionEndTime(0)
    , _sessionState(SessionState::NeverStarted)
    , _currentPhase(Phase::Idle)
    , _nextEventId(1)
    , _downloadTotal(0)
    , _decompressTotal(0)
    , _writeTotal(0)
    , _verifyTotal(0)
    , _hasSystemInfo(false)
{
    std::memset(_phaseStartTimes, 0, sizeof(_phaseStartTimes));
    std::memset(_lastSampleTime, 0, sizeof(_lastSampleTime));
    std::memset(&_systemInfo, 0, sizeof(_systemInfo));
}

void PerformanceStats::startSession(const QString &imageName, quint64 imageSize, const QString &deviceName)
{
    QMutexLocker locker(&_mutex);
    
    // If this is the very first session, initialise capacity
    if (_events.isEmpty()) {
        _downloadSamples.reserve(1000);
        _decompressSamples.reserve(2000);
        _writeSamples.reserve(2000);
        _verifySamples.reserve(1000);
        _events.reserve(100);
    }
    
    // Emit CycleStart event to mark the beginning of a new imaging cycle
    // This allows multiple cycles to be captured and analysed separately
    TimedEvent cycleStartEvent;
    cycleStartEvent.type = EventType::CycleStart;
    cycleStartEvent.startMs = _sessionActive ? static_cast<uint32_t>(_sessionTimer.elapsed()) : 0;
    cycleStartEvent.durationMs = 0;
    cycleStartEvent.metadata = QString("image: %1; device: %2; size: %3")
                                .arg(imageName)
                                .arg(deviceName)
                                .arg(imageSize);
    cycleStartEvent.success = true;
    cycleStartEvent.bytesTransferred = imageSize;
    _events.append(cycleStartEvent);
    
    // Update session state
    _imageName = imageName;
    _deviceName = deviceName;
    _imageSize = imageSize;
    _sessionStartTime = QDateTime::currentMSecsSinceEpoch();
    _sessionEndTime = 0;
    _sessionState = SessionState::InProgress;
    _errorMessage.clear();
    
    _currentPhase = Phase::Idle;
    std::memset(_phaseStartTimes, 0, sizeof(_phaseStartTimes));
    std::memset(_lastSampleTime, 0, sizeof(_lastSampleTime));
    
    _downloadTotal = 0;
    _decompressTotal = 0;
    _writeTotal = 0;
    _verifyTotal = 0;
    
    _hasSystemInfo = false;
    std::memset(&_systemInfo, 0, sizeof(_systemInfo));
    
    // Start/restart the session timer for this cycle
    _sessionTimer.start();
    _sessionActive = true;
    
    qDebug() << "PerformanceStats: Started cycle for" << imageName 
             << "size:" << imageSize << "device:" << deviceName
             << "total events:" << _events.size();
}

void PerformanceStats::reset()
{
    QMutexLocker locker(&_mutex);
    
    // Clear all accumulated data
    _events.clear();
    _pendingEvents.clear();
    _downloadSamples.clear();
    _decompressSamples.clear();
    _writeSamples.clear();
    _verifySamples.clear();
    
    _imageName.clear();
    _deviceName.clear();
    _imageSize = 0;
    _sessionStartTime = 0;
    _sessionEndTime = 0;
    _sessionState = SessionState::NeverStarted;
    _errorMessage.clear();
    _sessionActive = false;
    
    _currentPhase = Phase::Idle;
    std::memset(_phaseStartTimes, 0, sizeof(_phaseStartTimes));
    std::memset(_lastSampleTime, 0, sizeof(_lastSampleTime));
    
    _downloadTotal = 0;
    _decompressTotal = 0;
    _writeTotal = 0;
    _verifyTotal = 0;
    
    _nextEventId = 1;
    _hasSystemInfo = false;
    std::memset(&_systemInfo, 0, sizeof(_systemInfo));
    
    qDebug() << "PerformanceStats: Reset all data";
}

void PerformanceStats::setSystemInfo(const SystemInfo &info)
{
    QMutexLocker locker(&_mutex);
    _systemInfo = info;
    _hasSystemInfo = true;
    
    qDebug() << "PerformanceStats: System info set - RAM:" 
             << (info.totalMemoryBytes / (1024*1024)) << "MB total,"
             << (info.availableMemoryBytes / (1024*1024)) << "MB available"
             << "Device:" << info.deviceDescription;
}

void PerformanceStats::updateDirectIOEnabled(bool enabled)
{
    QMutexLocker locker(&_mutex);
    _systemInfo.directIOEnabled = enabled;
    qDebug() << "PerformanceStats: Direct I/O state updated to" << (enabled ? "enabled" : "disabled");
}

void PerformanceStats::endSession(bool success, const QString &errorMessage)
{
    QMutexLocker locker(&_mutex);
    
    if (!_sessionActive)
        return;
    
    // Determine session state based on success and error message
    SessionState newState;
    if (success) {
        newState = SessionState::Succeeded;
    } else {
        // Check if this was a cancellation vs a failure
        // Common cancellation indicators: "Cancel", "cancelled", "removed", "user"
        QString lowerError = errorMessage.toLower();
        if (lowerError.contains("cancel") || 
            lowerError.contains("removed") ||
            lowerError.contains("user")) {
            newState = SessionState::Cancelled;
        } else {
            newState = SessionState::Failed;
        }
    }
    
    // Emit CycleEnd event to mark the end of this imaging cycle
    TimedEvent cycleEndEvent;
    cycleEndEvent.type = EventType::CycleEnd;
    cycleEndEvent.startMs = static_cast<uint32_t>(_sessionTimer.elapsed());
    cycleEndEvent.durationMs = 0;
    cycleEndEvent.metadata = success ? "completed" : QString("failed: %1").arg(errorMessage);
    cycleEndEvent.success = success;
    cycleEndEvent.bytesTransferred = 0;
    _events.append(cycleEndEvent);
    
    _sessionEndTime = QDateTime::currentMSecsSinceEpoch();
    _sessionState = newState;
    _errorMessage = errorMessage;
    _sessionActive = false;
    _currentPhase = Phase::Idle;
    
    qDebug() << "PerformanceStats: Cycle ended, state:" << sessionStateName(newState)
             << "events:" << _events.size()
             << "samples: dl=" << _downloadSamples.size()
             << "dec=" << _decompressSamples.size()
             << "wr=" << _writeSamples.size()
             << "vfy=" << _verifySamples.size();
}

bool PerformanceStats::isSessionActive() const
{
    QMutexLocker locker(&_mutex);
    return _sessionActive;
}

PerformanceStats::SessionState PerformanceStats::sessionState() const
{
    QMutexLocker locker(&_mutex);
    return _sessionState;
}

int PerformanceStats::beginEvent(EventType type, const QString &metadata)
{
    QMutexLocker locker(&_mutex);
    
    int eventId = _nextEventId++;
    PendingEvent pending;
    pending.type = type;
    pending.startTime = _sessionActive ? _sessionTimer.elapsed() : 0;
    pending.metadata = metadata;
    _pendingEvents[eventId] = pending;
    
    return eventId;
}

void PerformanceStats::endEvent(int eventId, bool success, const QString &additionalMetadata)
{
    QMutexLocker locker(&_mutex);
    
    auto it = _pendingEvents.find(eventId);
    if (it == _pendingEvents.end()) {
        qWarning() << "PerformanceStats: endEvent called with unknown eventId:" << eventId;
        return;
    }
    
    PendingEvent &pending = it.value();
    qint64 endTime = _sessionActive ? _sessionTimer.elapsed() : 0;
    qint64 duration = endTime - pending.startTime;
    
    TimedEvent event;
    event.type = pending.type;
    event.startMs = static_cast<uint32_t>(pending.startTime);
    event.durationMs = static_cast<uint32_t>(qMax(0LL, duration));
    event.metadata = pending.metadata;
    if (!additionalMetadata.isEmpty()) {
        if (!event.metadata.isEmpty())
            event.metadata += "; ";
        event.metadata += additionalMetadata;
    }
    event.success = success;
    event.bytesTransferred = 0;
    
    _events.append(event);
    _pendingEvents.erase(it);
}

void PerformanceStats::recordEvent(EventType type, uint32_t durationMs, bool success, const QString &metadata)
{
    QMutexLocker locker(&_mutex);
    
    TimedEvent event;
    event.type = type;
    event.startMs = _sessionActive ? static_cast<uint32_t>(_sessionTimer.elapsed()) - durationMs : 0;
    event.durationMs = durationMs;
    event.metadata = metadata;
    event.success = success;
    event.bytesTransferred = 0;
    
    _events.append(event);
}

void PerformanceStats::recordTransferEvent(EventType type, uint32_t durationMs, uint64_t bytesTransferred,
                                          bool success, const QString &metadata)
{
    QMutexLocker locker(&_mutex);
    
    TimedEvent event;
    event.type = type;
    event.startMs = _sessionActive ? static_cast<uint32_t>(_sessionTimer.elapsed()) - durationMs : 0;
    event.durationMs = durationMs;
    event.metadata = metadata;
    event.success = success;
    event.bytesTransferred = bytesTransferred;
    
    _events.append(event);
}

void PerformanceStats::addEvent(const TimedEvent &event)
{
    QMutexLocker locker(&_mutex);
    _events.append(event);
}

void PerformanceStats::recordDownloadProgress(quint64 bytesNow, quint64 bytesTotal)
{
    addRawSample(Phase::Downloading, bytesNow, bytesTotal);
    
    QMutexLocker locker(&_mutex);
    _downloadTotal = bytesTotal;
}

void PerformanceStats::recordDecompressProgress(quint64 bytesDecompressed, quint64 bytesTotal)
{
    addRawSample(Phase::Decompressing, bytesDecompressed, bytesTotal);
    
    QMutexLocker locker(&_mutex);
    _decompressTotal = bytesTotal;
}

void PerformanceStats::recordWriteProgress(quint64 bytesWritten, quint64 bytesTotal)
{
    addRawSample(Phase::Writing, bytesWritten, bytesTotal);
    
    QMutexLocker locker(&_mutex);
    _writeTotal = bytesTotal;
}

void PerformanceStats::recordVerifyProgress(quint64 bytesVerified, quint64 bytesTotal)
{
    addRawSample(Phase::Verifying, bytesVerified, bytesTotal);
    
    QMutexLocker locker(&_mutex);
    _verifyTotal = bytesTotal;
}

void PerformanceStats::recordFinalising()
{
    QMutexLocker locker(&_mutex);
    
    if (!_sessionActive)
        return;
    
    _currentPhase = Phase::Finalising;
    _phaseStartTimes[static_cast<int>(Phase::Finalising)] = _sessionTimer.elapsed();
}

void PerformanceStats::addRawSample(Phase phase, quint64 bytesNow, quint64 bytesTotal)
{
    QMutexLocker locker(&_mutex);
    
    if (!_sessionActive)
        return;
    
    // Map phase to array index: 0=download, 1=decompress, 2=write, 3=verify
    int phaseIdx = -1;
    switch (phase) {
        case Phase::Downloading: phaseIdx = 0; break;
        case Phase::Decompressing: phaseIdx = 1; break;
        case Phase::Writing: phaseIdx = 2; break;
        case Phase::Verifying: phaseIdx = 3; break;
        default: return;
    }
    
    qint64 currentTime = _sessionTimer.elapsed();
    
    // Track phase transitions
    if (phase != _currentPhase) {
        _currentPhase = phase;
        _phaseStartTimes[static_cast<int>(phase)] = currentTime;
        _lastSampleTime[phaseIdx] = 0;  // Reset rate limiting for new phase
    }
    
    // Rate limit samples - this is the only check, very fast
    if (currentTime - _lastSampleTime[phaseIdx] < MIN_SAMPLE_INTERVAL_MS)
        return;
    
    // Get appropriate vector
    QVector<RawSample> *samples = nullptr;
    switch (phase) {
        case Phase::Downloading: samples = &_downloadSamples; break;
        case Phase::Decompressing: samples = &_decompressSamples; break;
        case Phase::Writing: samples = &_writeSamples; break;
        case Phase::Verifying: samples = &_verifySamples; break;
        default: return;
    }
    
    // Check capacity limit
    if (samples->size() >= MAX_SAMPLES_PER_PHASE)
        return;
    
    // Store raw sample - minimal work
    RawSample sample;
    sample.timestampMs = static_cast<uint32_t>(currentTime);
    sample.bytesProcessed = bytesNow;
    samples->append(sample);
    
    _lastSampleTime[phaseIdx] = currentTime;
}

bool PerformanceStats::hasData() const
{
    QMutexLocker locker(&_mutex);
    return !_events.isEmpty() || 
           !_downloadSamples.isEmpty() || 
           !_decompressSamples.isEmpty() ||
           !_writeSamples.isEmpty() || 
           !_verifySamples.isEmpty();
}

bool PerformanceStats::hasImagingData() const
{
    QMutexLocker locker(&_mutex);
    
    // Has imaging data if:
    // 1. A session was started (state is not NeverStarted), OR
    // 2. We have actual throughput samples from imaging operations
    return _sessionState != SessionState::NeverStarted ||
           !_downloadSamples.isEmpty() || 
           !_decompressSamples.isEmpty() ||
           !_writeSamples.isEmpty() || 
           !_verifySamples.isEmpty();
}

QString PerformanceStats::eventTypeName(EventType type)
{
    switch (type) {
        // Network & OS list
        case EventType::OsListFetch: return "osListFetch";
        case EventType::OsListParse: return "osListParse";
        case EventType::SublistFetch: return "sublistFetch";
        case EventType::NetworkLatency: return "networkLatency";
        case EventType::NetworkRetry: return "networkRetry";
        case EventType::NetworkConnectionStats: return "networkConnectionStats";
        
        // Drive operations
        case EventType::DriveListPoll: return "driveListPoll";
        case EventType::DriveOpen: return "driveOpen";
        case EventType::DriveAuthorization: return "driveAuthorization";
        case EventType::DriveMbrZeroing: return "driveMbrZeroing";
        case EventType::DirectIOAttempt: return "directIOAttempt";
        case EventType::DriveUnmount: return "driveUnmount";
        case EventType::DriveUnmountVolumes: return "driveUnmountVolumes";
        case EventType::DriveDiskClean: return "driveDiskClean";
        case EventType::DriveRescan: return "driveRescan";
        case EventType::DriveFormat: return "driveFormat";
        
        // Cache operations
        case EventType::CacheLookup: return "cacheLookup";
        case EventType::CacheVerification: return "cacheVerification";
        case EventType::CacheWrite: return "cacheWrite";
        case EventType::CacheFlush: return "cacheFlush";
        
        // Memory management
        case EventType::MemoryAllocation: return "memoryAllocation";
        case EventType::BufferResize: return "bufferResize";
        case EventType::PeriodicSync: return "periodicSync";
        case EventType::RingBufferStarvation: return "ringBufferStarvation";
        
        // Image processing
        case EventType::ImageDecompressInit: return "imageDecompressInit";
        case EventType::ImageExtraction: return "imageExtraction";
        case EventType::HashComputation: return "hashComputation";
        
        // Pipeline timing
        case EventType::PipelineDecompressionTime: return "pipelineDecompressionTime";
        case EventType::PipelineWriteWaitTime: return "pipelineWriteWaitTime";
        case EventType::PipelineRingBufferWaitTime: return "pipelineRingBufferWaitTime";
        case EventType::WriteRingBufferStats: return "writeRingBufferStats";
        
        // Write timing breakdown (detailed instrumentation)
        case EventType::WriteTimingBreakdown: return "writeTimingBreakdown";
        case EventType::WriteSizeDistribution: return "writeSizeDistribution";
        case EventType::WriteAfterSyncImpact: return "writeAfterSyncImpact";
        case EventType::AsyncIOConfig: return "asyncIOConfig";
        case EventType::AsyncIOTiming: return "asyncIOTiming";
        
        // Cycle boundaries
        case EventType::CycleStart: return "cycleStart";
        case EventType::CycleEnd: return "cycleEnd";
        
        // Stall detection
        case EventType::ProgressStall: return "progressStall";
        case EventType::MemoryAllocationFailure: return "memoryAllocationFailure";
        case EventType::DeviceIOTimeout: return "deviceIOTimeout";
        case EventType::QueueDepthReduction: return "queueDepthReduction";
        case EventType::SyncFallbackActivated: return "syncFallbackActivated";
        case EventType::DrainAndHotSwap: return "drainAndHotSwap";
        case EventType::WatchdogRecovery: return "watchdogRecovery";
        
        // Customisation
        case EventType::Customisation: return "customisation";
        case EventType::CloudInitGeneration: return "cloudInitGeneration";
        case EventType::FirstRunGeneration: return "firstRunGeneration";
        case EventType::SecureBootSetup: return "secureBootSetup";
        
        // Finalisation
        case EventType::PartitionTableWrite: return "partitionTableWrite";
        case EventType::FatPartitionSetup: return "fatPartitionSetup";
        case EventType::FinalSync: return "finalSync";
        case EventType::DeviceClose: return "deviceClose";
        
        // UI operations
        case EventType::FileDialogOpen: return "fileDialogOpen";
        
        default: return "unknown";
    }
}

QString PerformanceStats::sessionStateName(SessionState state)
{
    switch (state) {
        case SessionState::NeverStarted: return "never_started";
        case SessionState::InProgress: return "in_progress";
        case SessionState::Succeeded: return "succeeded";
        case SessionState::Failed: return "failed";
        case SessionState::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

int PerformanceStats::getThroughputBucket(uint32_t kbps) const
{
    // Logarithmic buckets in MB/s: 0-1, 1-2, 2-4, 4-8, 8-16, 16-32, 32-64, 64-128, 128-256, 256-512, 512-1024, 1024+
    uint32_t mbps = kbps / 1024;
    
    if (mbps < 1) return 0;
    if (mbps < 2) return 1;
    if (mbps < 4) return 2;
    if (mbps < 8) return 3;
    if (mbps < 16) return 4;
    if (mbps < 32) return 5;
    if (mbps < 64) return 6;
    if (mbps < 128) return 7;
    if (mbps < 256) return 8;
    if (mbps < 512) return 9;
    if (mbps < 1024) return 10;
    return 11;
}

QJsonArray PerformanceStats::buildHistogramForPhase(const QVector<RawSample> &samples) const
{
    // Build time-series histogram from raw samples
    // This is the complex processing - only done at export time
    
    QJsonArray result;
    if (samples.size() < 2)
        return result;
    
    // Process samples into 1-second windows
    int windowStart = 0;
    uint32_t windowStartTime = samples[0].timestampMs;
    
    while (windowStart < samples.size()) {
        // Find end of current window
        int windowEnd = windowStart;
        while (windowEnd < samples.size() && 
               samples[windowEnd].timestampMs - windowStartTime < HISTOGRAM_WINDOW_MS) {
            windowEnd++;
        }
        
        // Build histogram for this window
        std::array<int, HISTOGRAM_BUCKETS> counts = {};
        uint32_t minKBps = UINT32_MAX, maxKBps = 0;
        uint64_t sumKBps = 0;
        int throughputSamples = 0;
        
        for (int i = windowStart + 1; i < windowEnd; ++i) {
            const RawSample &prev = samples[i - 1];
            const RawSample &curr = samples[i];
            
            if (curr.timestampMs > prev.timestampMs && curr.bytesProcessed > prev.bytesProcessed) {
                uint64_t bytesDelta = curr.bytesProcessed - prev.bytesProcessed;
                uint32_t timeDelta = curr.timestampMs - prev.timestampMs;
                uint32_t kbps = static_cast<uint32_t>((bytesDelta * 1000) / (static_cast<uint64_t>(timeDelta) * 1024));
                
                int bucket = getThroughputBucket(kbps);
                counts[bucket]++;
                minKBps = qMin(minKBps, kbps);
                maxKBps = qMax(maxKBps, kbps);
                sumKBps += kbps;
                throughputSamples++;
            }
        }
        
        if (throughputSamples > 0) {
            // Output: [timestampMs, minKBps, maxKBps, avgKBps, bucket0, bucket1, ..., bucket11]
            QJsonArray slice;
            slice.append(static_cast<qint64>(windowStartTime));
            slice.append(static_cast<qint64>(minKBps == UINT32_MAX ? 0 : minKBps));
            slice.append(static_cast<qint64>(maxKBps));
            slice.append(static_cast<qint64>(sumKBps / throughputSamples));
            for (int c : counts) {
                slice.append(c);
            }
            result.append(slice);
        }
        
        // Move to next window
        if (windowEnd >= samples.size())
            break;
        windowStart = windowEnd;
        windowStartTime = samples[windowStart].timestampMs;
    }
    
    return result;
}

QJsonObject PerformanceStats::buildHistograms() const
{
    // Build all histograms - complex processing done only at export
    QJsonObject histograms;
    
    if (!_downloadSamples.isEmpty()) {
        histograms["download"] = buildHistogramForPhase(_downloadSamples);
    }
    if (!_decompressSamples.isEmpty()) {
        histograms["decompress"] = buildHistogramForPhase(_decompressSamples);
    }
    if (!_writeSamples.isEmpty()) {
        histograms["write"] = buildHistogramForPhase(_writeSamples);
    }
    if (!_verifySamples.isEmpty()) {
        histograms["verify"] = buildHistogramForPhase(_verifySamples);
    }
    
    return histograms;
}

QJsonObject PerformanceStats::buildSummary() const
{
    QJsonObject summary;
    summary["imageName"] = _imageName;
    summary["deviceName"] = _deviceName;
    summary["imageSize"] = static_cast<qint64>(_imageSize);
    summary["sessionStartTime"] = _sessionStartTime;
    summary["sessionEndTime"] = _sessionEndTime;
    summary["durationMs"] = _sessionEndTime > 0 ? _sessionEndTime - _sessionStartTime : 0;
    
    // Include both the detailed state and legacy success boolean for compatibility
    summary["sessionState"] = sessionStateName(_sessionState);
    summary["success"] = (_sessionState == SessionState::Succeeded);
    
    if (!_errorMessage.isEmpty()) {
        summary["errorMessage"] = _errorMessage;
    }
    
    // Event summary by type
    QJsonObject eventSummary;
    QMap<EventType, QVector<uint32_t>> eventDurations;
    for (const TimedEvent &e : _events) {
        eventDurations[e.type].append(e.durationMs);
    }
    
    for (auto it = eventDurations.begin(); it != eventDurations.end(); ++it) {
        const QVector<uint32_t> &durations = it.value();
        QJsonObject stats;
        stats["count"] = durations.size();
        
        uint64_t sum = 0;
        uint32_t minVal = UINT32_MAX, maxVal = 0;
        for (uint32_t d : durations) {
            sum += d;
            minVal = qMin(minVal, d);
            maxVal = qMax(maxVal, d);
        }
        stats["totalMs"] = static_cast<qint64>(sum);
        stats["avgMs"] = durations.isEmpty() ? 0 : static_cast<qint64>(sum / durations.size());
        stats["minMs"] = durations.isEmpty() ? 0 : static_cast<qint64>(minVal);
        stats["maxMs"] = static_cast<qint64>(maxVal);
        
        eventSummary[eventTypeName(it.key())] = stats;
    }
    summary["events"] = eventSummary;
    
    // Phase statistics (calculated from raw samples)
    auto buildPhaseStats = [this](const QVector<RawSample> &samples, quint64 totalBytes) -> QJsonObject {
        QJsonObject stats;
        if (samples.isEmpty())
            return stats;
        
        stats["sampleCount"] = samples.size();
        stats["bytesTotal"] = static_cast<qint64>(totalBytes);
        
        if (samples.size() >= 2) {
            stats["durationMs"] = static_cast<qint64>(
                samples.last().timestampMs - samples.first().timestampMs
            );
            
            // Calculate throughput statistics from samples
            uint32_t minKBps = UINT32_MAX, maxKBps = 0;
            uint64_t sumKBps = 0;
            int count = 0;
            
            for (int i = 1; i < samples.size(); ++i) {
                const RawSample &prev = samples[i - 1];
                const RawSample &curr = samples[i];
                
                if (curr.timestampMs > prev.timestampMs && curr.bytesProcessed > prev.bytesProcessed) {
                    uint64_t bytesDelta = curr.bytesProcessed - prev.bytesProcessed;
                    uint32_t timeDelta = curr.timestampMs - prev.timestampMs;
                    uint32_t kbps = static_cast<uint32_t>((bytesDelta * 1000) / (static_cast<uint64_t>(timeDelta) * 1024));
                    
                    minKBps = qMin(minKBps, kbps);
                    maxKBps = qMax(maxKBps, kbps);
                    sumKBps += kbps;
                    count++;
                }
            }
            
            if (count > 0) {
                stats["minThroughputKBps"] = static_cast<qint64>(minKBps);
                stats["maxThroughputKBps"] = static_cast<qint64>(maxKBps);
                stats["avgThroughputKBps"] = static_cast<qint64>(sumKBps / count);
            }
        }
        
        return stats;
    };
    
    QJsonObject phases;
    phases["download"] = buildPhaseStats(_downloadSamples, _downloadTotal);
    phases["decompress"] = buildPhaseStats(_decompressSamples, _decompressTotal);
    phases["write"] = buildPhaseStats(_writeSamples, _writeTotal);
    phases["verify"] = buildPhaseStats(_verifySamples, _verifyTotal);
    summary["phases"] = phases;
    
    return summary;
}

QJsonDocument PerformanceStats::exportToJson() const
{
    QMutexLocker locker(&_mutex);
    
    // All complex processing happens here, triggered by user action (keyboard shortcut)
    
    QJsonObject root;
    root["version"] = 3;
    root["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // Build summary (includes event and phase statistics)
    root["summary"] = buildSummary();
    
    // System information (no unique identifiers)
    if (_hasSystemInfo) {
        QJsonObject sysInfo;
        
        // Memory
        QJsonObject memory;
        memory["totalBytes"] = static_cast<qint64>(_systemInfo.totalMemoryBytes);
        memory["availableBytes"] = static_cast<qint64>(_systemInfo.availableMemoryBytes);
        memory["totalMB"] = static_cast<qint64>(_systemInfo.totalMemoryBytes / (1024 * 1024));
        memory["availableMB"] = static_cast<qint64>(_systemInfo.availableMemoryBytes / (1024 * 1024));
        sysInfo["memory"] = memory;
        
        // Target device (no serial numbers or unique IDs)
        QJsonObject device;
        device["path"] = _systemInfo.devicePath;
        device["sizeBytes"] = static_cast<qint64>(_systemInfo.deviceSizeBytes);
        device["sizeMB"] = static_cast<qint64>(_systemInfo.deviceSizeBytes / (1024 * 1024));
        device["description"] = _systemInfo.deviceDescription;
        device["isUsb"] = _systemInfo.deviceIsUsb;
        device["isRemovable"] = _systemInfo.deviceIsRemovable;
        sysInfo["targetDevice"] = device;
        
        // Platform
        QJsonObject platform;
        platform["os"] = _systemInfo.osName;
        platform["osVersion"] = _systemInfo.osVersion;
        platform["cpuArchitecture"] = _systemInfo.cpuArchitecture;
        platform["cpuCores"] = _systemInfo.cpuCoreCount;
        sysInfo["platform"] = platform;
        
        // Imager build info
        QJsonObject imager;
        imager["version"] = _systemInfo.imagerVersion;
        if (!_systemInfo.imagerBuildType.isEmpty())
            imager["buildType"] = _systemInfo.imagerBuildType;
        if (!_systemInfo.imagerBinarySha256.isEmpty())
            imager["binarySha256"] = _systemInfo.imagerBinarySha256;
        if (!_systemInfo.qtVersion.isEmpty())
            imager["qtRuntime"] = _systemInfo.qtVersion;
        if (!_systemInfo.qtBuildVersion.isEmpty())
            imager["qtBuild"] = _systemInfo.qtBuildVersion;
        sysInfo["imager"] = imager;
        
        // Write configuration (helps diagnose performance issues like sync overhead)
        QJsonObject writeConfig;
        writeConfig["directIOEnabled"] = _systemInfo.directIOEnabled;
        writeConfig["periodicSyncEnabled"] = _systemInfo.periodicSyncEnabled;
        if (_systemInfo.periodicSyncEnabled) {
            writeConfig["syncIntervalBytes"] = _systemInfo.syncIntervalBytes;
            writeConfig["syncIntervalMB"] = _systemInfo.syncIntervalBytes / (1024 * 1024);
            writeConfig["syncIntervalMs"] = _systemInfo.syncIntervalMs;
        }
        if (!_systemInfo.memoryTier.isEmpty())
            writeConfig["memoryTier"] = _systemInfo.memoryTier;
        
        // Buffer configuration
        QJsonObject buffers;
        buffers["writeBufferBytes"] = _systemInfo.writeBufferSize;
        buffers["writeBufferKB"] = _systemInfo.writeBufferSize / 1024;
        buffers["inputBufferBytes"] = _systemInfo.inputBufferSize;
        buffers["inputBufferKB"] = _systemInfo.inputBufferSize / 1024;
        buffers["inputRingBufferSlots"] = _systemInfo.inputRingBufferSlots;
        buffers["writeRingBufferSlots"] = _systemInfo.writeRingBufferSlots;
        writeConfig["buffers"] = buffers;
        
        sysInfo["writeConfig"] = writeConfig;
        
        root["system"] = sysInfo;
    }
    
    // Events with full detail
    QJsonArray eventsArray;
    for (const TimedEvent &e : _events) {
        QJsonObject eventObj;
        eventObj["type"] = eventTypeName(e.type);
        eventObj["startMs"] = static_cast<qint64>(e.startMs);
        eventObj["durationMs"] = static_cast<qint64>(e.durationMs);
        eventObj["success"] = e.success;
        if (!e.metadata.isEmpty()) {
            eventObj["metadata"] = e.metadata;
        }
        // Include bytes transferred and calculated throughput for network/IO events
        if (e.bytesTransferred > 0) {
            eventObj["bytesTransferred"] = static_cast<qint64>(e.bytesTransferred);
            if (e.durationMs > 0) {
                // Calculate throughput in KB/s
                uint64_t throughputKBps = (e.bytesTransferred * 1000) / (static_cast<uint64_t>(e.durationMs) * 1024);
                eventObj["throughputKBps"] = static_cast<qint64>(throughputKBps);
            }
        }
        eventsArray.append(eventObj);
    }
    root["events"] = eventsArray;
    
    // Build time-series histograms (complex processing)
    root["histograms"] = buildHistograms();
    
    // Schema for parsing
    QJsonObject schema;
    schema["histogramSliceFormat"] = QJsonArray({
        "timestampMs", "minKBps", "maxKBps", "avgKBps",
        "bucket_0-1MB", "bucket_1-2MB", "bucket_2-4MB", "bucket_4-8MB",
        "bucket_8-16MB", "bucket_16-32MB", "bucket_32-64MB", "bucket_64-128MB",
        "bucket_128-256MB", "bucket_256-512MB", "bucket_512-1024MB", "bucket_1024+MB"
    });
    schema["histogramWindowMs"] = HISTOGRAM_WINDOW_MS;
    schema["throughputUnit"] = "KB/s";
    root["schema"] = schema;
    
    return QJsonDocument(root);
}

bool PerformanceStats::exportToFile(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "PerformanceStats: Failed to open file for writing:" << filePath;
        return false;
    }
    
    QJsonDocument doc = exportToJson();
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
    qDebug() << "PerformanceStats: Exported data to" << filePath;
    return true;
}
