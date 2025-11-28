/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef PERFORMANCESTATS_H
#define PERFORMANCESTATS_H

#include <QObject>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <QMap>
#include <QMutex>
#include <array>

/**
 * @brief Lightweight performance data capture for all imaging operations
 * 
 * Design principle: Collect raw data with minimal overhead during operations.
 * All complex processing (histograms, statistics) is deferred to export time.
 * 
 * Captures:
 * - Discrete events: OS list fetch, drive open, customisation, etc.
 * - Raw progress samples: Timestamp + bytes (processing deferred to export)
 */
class PerformanceStats : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Discrete event types for timing measurements
     */
    enum class EventType : uint8_t {
        // Network & OS list
        OsListFetch,           // Time to fetch OS list from server
        OsListParse,           // Time to parse OS list JSON
        SublistFetch,          // Time to fetch a sublist
        NetworkLatency,        // Network round-trip measurement
        
        // Drive operations
        DriveListPoll,         // Time for drive enumeration
        DriveOpen,             // Time to open/prepare drive for writing
        DriveUnmount,          // Time to unmount drive partitions
        DriveFormat,           // Time to format drive (for multi-file zips)
        
        // Cache operations
        CacheLookup,           // Time to look up file in cache
        CacheVerification,     // Time to verify cached file hash
        CacheWrite,            // Time to write data to cache file
        CacheFlush,            // Time to flush cache to disk
        
        // Memory management
        MemoryAllocation,      // Time for large memory allocations
        BufferResize,          // Time to resize buffers
        PageCacheFlush,        // Time to flush system page cache
        
        // Image processing
        ImageDecompressInit,   // Time to initialise decompression
        ImageExtraction,       // Time for archive extraction setup
        HashComputation,       // Time spent on hash computations
        
        // Customisation
        Customisation,         // Time to apply customisation (config, firstrun, etc.)
        CloudInitGeneration,   // Time to generate cloud-init config
        FirstRunGeneration,    // Time to generate firstrun script
        SecureBootSetup,       // Time to set up secure boot files
        
        // Finalisation
        PartitionTableWrite,   // Time to write partition table
        FatPartitionSetup,     // Time to set up FAT partition
        FinalSync,             // Time for final sync/flush
        DeviceClose,           // Time to close device handles
        
        _Count                 // Sentinel for array sizing
    };
    Q_ENUM(EventType)

    /**
     * @brief Phase types for throughput tracking
     */
    enum class Phase : uint8_t {
        Idle = 0,
        Downloading = 1,
        Writing = 2,
        Verifying = 3,
        Finalising = 4
    };
    Q_ENUM(Phase)

    /**
     * @brief Minimal raw sample - just timestamp and bytes (12 bytes)
     * Throughput calculation deferred to export time
     */
    struct RawSample {
        uint32_t timestampMs;      // Milliseconds from session start
        uint64_t bytesProcessed;   // Total bytes at this point
    };

    /**
     * @brief Timed event record
     */
    struct TimedEvent {
        EventType type;
        uint32_t startMs;      // Start time from session start
        uint32_t durationMs;   // Duration of event
        QString metadata;      // Optional metadata (e.g., URL, error message)
        bool success;
        uint64_t bytesTransferred;  // Bytes transferred (for network/IO events)
    };

    explicit PerformanceStats(QObject *parent = nullptr);
    ~PerformanceStats() = default;

    // ===== Session Management =====
    
    /**
     * @brief Start a new recording session (clears previous data)
     */
    void startSession(const QString &imageName, quint64 imageSize, const QString &deviceName);
    
    /**
     * @brief End the current session
     */
    void endSession(bool success, const QString &errorMessage = QString());
    
    /**
     * @brief Check if a session is active
     */
    bool isSessionActive() const;

    // ===== Event Timing =====
    
    /**
     * @brief Start timing a discrete event
     * @return Event ID for use with endEvent()
     */
    int beginEvent(EventType type, const QString &metadata = QString());
    
    /**
     * @brief End timing a discrete event
     */
    void endEvent(int eventId, bool success = true, const QString &additionalMetadata = QString());
    
    /**
     * @brief Record a complete event with known duration
     */
    void recordEvent(EventType type, uint32_t durationMs, bool success = true, const QString &metadata = QString());
    
    /**
     * @brief Record a network/IO event with bytes transferred (for throughput calculation)
     */
    void recordTransferEvent(EventType type, uint32_t durationMs, uint64_t bytesTransferred, 
                            bool success = true, const QString &metadata = QString());

    // ===== Lightweight Progress Recording =====
    // These just store raw (timestamp, bytes) pairs - very fast
    
    /**
     * @brief Record download progress (lightweight - just stores raw sample)
     */
    void recordDownloadProgress(quint64 bytesNow, quint64 bytesTotal);
    
    /**
     * @brief Record write progress (lightweight - just stores raw sample)
     */
    void recordWriteProgress(quint64 bytesWritten, quint64 bytesTotal);
    
    /**
     * @brief Record verification progress (lightweight - just stores raw sample)
     */
    void recordVerifyProgress(quint64 bytesVerified, quint64 bytesTotal);
    
    /**
     * @brief Mark operation as finalising
     */
    void recordFinalising();

    // ===== Export (Complex processing happens here) =====
    
    /**
     * @brief Check if there is data available to export
     */
    bool hasData() const;
    
    /**
     * @brief Export performance data to JSON format
     * NOTE: This is where all complex processing happens (histogram building, etc.)
     */
    QJsonDocument exportToJson() const;
    
    /**
     * @brief Export performance data to a file
     */
    bool exportToFile(const QString &filePath) const;
    
    /**
     * @brief Get current phase
     */
    Phase currentPhase() const { return _currentPhase; }

    /**
     * @brief Get event type name as string
     */
    static QString eventTypeName(EventType type);

private:
    // Minimum interval between samples (ms) to limit data volume
    static constexpr int MIN_SAMPLE_INTERVAL_MS = 100;
    // Maximum raw samples per phase
    static constexpr int MAX_SAMPLES_PER_PHASE = 6000;  // ~10 minutes at 100ms intervals
    
    // Histogram constants (used only at export time)
    static constexpr int HISTOGRAM_BUCKETS = 12;
    static constexpr int HISTOGRAM_WINDOW_MS = 1000;
    
    void addRawSample(Phase phase, quint64 bytesNow, quint64 bytesTotal);
    
    // These are called only during export - complex processing deferred
    QJsonObject buildSummary() const;
    QJsonObject buildHistograms() const;
    QJsonArray buildHistogramForPhase(const QVector<RawSample> &samples) const;
    int getThroughputBucket(uint32_t kbps) const;
    
    mutable QMutex _mutex;
    QElapsedTimer _sessionTimer;
    bool _sessionActive;

    // Session metadata
    QString _imageName;
    QString _deviceName;
    quint64 _imageSize;
    qint64 _sessionStartTime;  // Unix timestamp ms
    qint64 _sessionEndTime;
    bool _sessionSuccess;
    QString _errorMessage;

    // Phase tracking
    Phase _currentPhase;
    qint64 _phaseStartTimes[5];

    // Event tracking
    QVector<TimedEvent> _events;
    struct PendingEvent {
        EventType type;
        qint64 startTime;
        QString metadata;
    };
    QMap<int, PendingEvent> _pendingEvents;
    int _nextEventId;

    // Raw sample storage - minimal overhead during collection
    QVector<RawSample> _downloadSamples;
    QVector<RawSample> _writeSamples;
    QVector<RawSample> _verifySamples;
    
    // Totals for each phase
    quint64 _downloadTotal;
    quint64 _writeTotal;
    quint64 _verifyTotal;

    // Rate limiting state
    qint64 _lastSampleTime[3];  // Per-phase last sample time
};

#endif // PERFORMANCESTATS_H
