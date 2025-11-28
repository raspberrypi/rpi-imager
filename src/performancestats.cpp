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
    , _sessionSuccess(false)
    , _currentPhase(Phase::Idle)
    , _nextEventId(1)
    , _downloadTotal(0)
    , _writeTotal(0)
    , _verifyTotal(0)
{
    std::memset(_phaseStartTimes, 0, sizeof(_phaseStartTimes));
    std::memset(_lastSampleTime, 0, sizeof(_lastSampleTime));
}

void PerformanceStats::startSession(const QString &imageName, quint64 imageSize, const QString &deviceName)
{
    QMutexLocker locker(&_mutex);
    
    // Clear previous data
    _events.clear();
    _pendingEvents.clear();
    _downloadSamples.clear();
    _writeSamples.clear();
    _verifySamples.clear();
    
    // Reserve capacity for raw samples
    _downloadSamples.reserve(1000);
    _writeSamples.reserve(2000);
    _verifySamples.reserve(1000);
    _events.reserve(50);
    
    // Initialise session
    _imageName = imageName;
    _deviceName = deviceName;
    _imageSize = imageSize;
    _sessionStartTime = QDateTime::currentMSecsSinceEpoch();
    _sessionEndTime = 0;
    _sessionSuccess = false;
    _errorMessage.clear();
    
    _currentPhase = Phase::Idle;
    std::memset(_phaseStartTimes, 0, sizeof(_phaseStartTimes));
    std::memset(_lastSampleTime, 0, sizeof(_lastSampleTime));
    
    _downloadTotal = 0;
    _writeTotal = 0;
    _verifyTotal = 0;
    
    _nextEventId = 1;
    _sessionTimer.start();
    _sessionActive = true;
    
    qDebug() << "PerformanceStats: Started session for" << imageName 
             << "size:" << imageSize << "device:" << deviceName;
}

void PerformanceStats::endSession(bool success, const QString &errorMessage)
{
    QMutexLocker locker(&_mutex);
    
    if (!_sessionActive)
        return;
    
    _sessionEndTime = QDateTime::currentMSecsSinceEpoch();
    _sessionSuccess = success;
    _errorMessage = errorMessage;
    _sessionActive = false;
    _currentPhase = Phase::Idle;
    
    qDebug() << "PerformanceStats: Session ended, success:" << success
             << "events:" << _events.size()
             << "samples: dl=" << _downloadSamples.size()
             << "wr=" << _writeSamples.size()
             << "vfy=" << _verifySamples.size();
}

bool PerformanceStats::isSessionActive() const
{
    QMutexLocker locker(&_mutex);
    return _sessionActive;
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

void PerformanceStats::recordDownloadProgress(quint64 bytesNow, quint64 bytesTotal)
{
    addRawSample(Phase::Downloading, bytesNow, bytesTotal);
    
    QMutexLocker locker(&_mutex);
    _downloadTotal = bytesTotal;
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
    
    int phaseIdx = static_cast<int>(phase) - 1;  // 0=download, 1=write, 2=verify
    if (phaseIdx < 0 || phaseIdx > 2)
        return;
    
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
        
        // Drive operations
        case EventType::DriveListPoll: return "driveListPoll";
        case EventType::DriveOpen: return "driveOpen";
        case EventType::DriveUnmount: return "driveUnmount";
        case EventType::DriveFormat: return "driveFormat";
        
        // Cache operations
        case EventType::CacheLookup: return "cacheLookup";
        case EventType::CacheVerification: return "cacheVerification";
        case EventType::CacheWrite: return "cacheWrite";
        case EventType::CacheFlush: return "cacheFlush";
        
        // Memory management
        case EventType::MemoryAllocation: return "memoryAllocation";
        case EventType::BufferResize: return "bufferResize";
        case EventType::PageCacheFlush: return "pageCacheFlush";
        
        // Image processing
        case EventType::ImageDecompressInit: return "imageDecompressInit";
        case EventType::ImageExtraction: return "imageExtraction";
        case EventType::HashComputation: return "hashComputation";
        
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
    summary["success"] = _sessionSuccess;
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
    root["version"] = 2;
    root["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // Build summary (includes event and phase statistics)
    root["summary"] = buildSummary();
    
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
