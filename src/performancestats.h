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
        NetworkRetry,          // Network connection retry (with reason)
        NetworkConnectionStats,// CURL connection timing metrics
        
        // Drive operations
        DriveListPoll,         // Time for drive enumeration
        DriveOpen,             // Time to open/prepare drive for writing (overall)
        DriveAuthorization,    // Time for privilege escalation (macOS authopen, Linux sudo)
        DriveMbrZeroing,       // Time to zero first/last MB of drive (includes sync)
        DirectIOAttempt,       // Direct I/O attempt result (success/failure with error code)
        DriveUnmount,          // Time to unmount drive partitions (Linux/macOS)
        DriveUnmountVolumes,   // Time to unmount/lock volumes (Windows)
        DriveDiskClean,        // Time to clean disk/remove partitions (Windows)
        DriveRescan,           // Time to rescan disk after cleaning (Windows)
        DriveFormat,           // Time to format drive (for multi-file zips)
        
        // Cache operations
        CacheLookup,           // Time to look up file in cache
        CacheVerification,     // Time to verify cached file hash
        CacheWrite,            // Time to write data to cache file
        CacheFlush,            // Time to flush cache to disk
        
        // Memory management
        MemoryAllocation,      // Time for large memory allocations
        BufferResize,          // Time to resize buffers
        PeriodicSync,          // Periodic fsync during write (skipped when direct I/O enabled)
        RingBufferStarvation,  // Ring buffer starvation stats (producer/consumer waits)
        
        // Image processing
        ImageDecompressInit,   // Time to initialise decompression
        ImageExtraction,       // Time for archive extraction setup
        HashComputation,       // Time spent on hash computations
        
        // Pipeline timing (summary events emitted at end of extraction)
        PipelineDecompressionTime, // Total time spent in libarchive decompression
        PipelineWriteWaitTime,     // Total time blocked waiting for disk writes
        PipelineRingBufferWaitTime,// Total time waiting for ring buffer data (input buffer)
        WriteRingBufferStats,      // Write ring buffer stall statistics (decompress->write)
        
        // Write timing breakdown (detailed instrumentation for hypothesis testing)
        WriteTimingBreakdown,      // Detailed breakdown: syscall time, hash wait, sync time
        WriteSizeDistribution,     // Distribution of write chunk sizes
        WriteAfterSyncImpact,      // Throughput comparison before/after sync calls
        AsyncIOConfig,             // Async I/O configuration (enabled, supported, queue depth)
        AsyncIOTiming,             // Async I/O wall-clock time and per-write latency stats
        
        // Cycle boundaries (for multi-write sessions)
        CycleStart,            // Start of a new imaging cycle (metadata: image name, device)
        CycleEnd,              // End of an imaging cycle (metadata: success/failure reason)
        
        // Stall detection and recovery
        ProgressStall,         // Pipeline stalled - no progress for extended period
        MemoryAllocationFailure, // Failed to allocate memory for buffers
        DeviceIOTimeout,       // Device/OS failed to complete writes within timeout
        QueueDepthReduction,   // Async queue depth reduced due to high latency (metadata: old->new depth)
        SyncFallbackActivated, // Switched from async to sync I/O mode
        DrainAndHotSwap,       // Drained async queue and hot-swapped to sync (metadata: pending count, drain time)
        WatchdogRecovery,      // Watchdog triggered recovery action (metadata: action taken)
        
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
        
        // UI operations
        FileDialogOpen,        // Time to open native file dialog (with detailed breakdown)
        
        _Count                 // Sentinel for array sizing
    };
    Q_ENUM(EventType)

    /**
     * @brief Phase types for throughput tracking
     */
    enum class Phase : uint8_t {
        Idle = 0,
        Downloading = 1,
        Decompressing = 2,
        Writing = 3,
        Verifying = 4,
        Finalising = 5
    };
    Q_ENUM(Phase)

    /**
     * @brief Session state for tracking imaging operation lifecycle
     * 
     * Provides richer state than a simple success/fail boolean:
     * - NeverStarted: No imaging session was initiated (only background events captured)
     * - InProgress: Session started but not yet completed
     * - Succeeded: Session completed successfully
     * - Failed: Session ended with an error
     * - Cancelled: Session was cancelled by user or due to device removal
     */
    enum class SessionState : uint8_t {
        NeverStarted = 0,  // startSession() was never called
        InProgress = 1,    // startSession() called, endSession() not yet called
        Succeeded = 2,     // endSession(true) called
        Failed = 3,        // endSession(false) called with error
        Cancelled = 4      // endSession(false) called due to user cancel or device removal
    };
    Q_ENUM(SessionState)

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

    /**
     * @brief System information captured at session start (no unique identifiers)
     */
    struct SystemInfo {
        // Memory
        quint64 totalMemoryBytes;
        quint64 availableMemoryBytes;
        
        // Target storage device (no serial numbers)
        QString devicePath;
        quint64 deviceSizeBytes;
        QString deviceDescription;  // e.g., "USB Mass Storage Device"
        bool deviceIsUsb;
        bool deviceIsRemovable;
        
        // OS/Platform
        QString osName;
        QString osVersion;
        QString cpuArchitecture;
        int cpuCoreCount;
        
        // Imager version and build info
        QString imagerVersion;          // e.g., "v1.9.2" or "v1.9.2-15-gabcdef0"
        QString imagerBuildType;        // e.g., "Release", "Debug", "RelWithDebInfo"
        QString imagerBinarySha256;     // SHA256 of the executable binary
        QString qtVersion;              // Qt runtime version
        QString qtBuildVersion;         // Qt version used at compile time
        
        // Write configuration (helps diagnose performance issues)
        bool directIOEnabled;           // True if direct I/O (F_NOCACHE/O_DIRECT) is enabled
        bool periodicSyncEnabled;       // True if periodic sync is enabled (false if direct I/O)
        qint64 syncIntervalBytes;       // Sync interval in bytes (0 if periodic sync disabled)
        qint64 syncIntervalMs;          // Sync interval in milliseconds
        QString memoryTier;             // Memory tier description (e.g., "High memory (16384MB)")
        
        // Buffer configuration (helps diagnose buffer-related issues)
        qint64 writeBufferSize;         // Size of each write buffer in bytes
        qint64 inputBufferSize;         // Size of input (download) buffer in bytes
        int inputRingBufferSlots;       // Number of slots in input ring buffer
        int writeRingBufferSlots;       // Number of slots in write ring buffer
    };

    explicit PerformanceStats(QObject *parent = nullptr);
    ~PerformanceStats() = default;

    // ===== Session Management =====
    
    /**
     * @brief Start a new imaging cycle within the current capture session
     * Emits a CycleStart event - does NOT clear previous data.
     * Multiple cycles can be captured in a single session for analysis.
     */
    void startSession(const QString &imageName, quint64 imageSize, const QString &deviceName);
    
    /**
     * @brief Set system information for the session
     * Call after startSession() to add hardware context
     */
    void setSystemInfo(const SystemInfo &info);
    
    /**
     * @brief Update the direct I/O enabled state after device is opened
     * This is called when the actual direct I/O state is known (after file open)
     */
    void updateDirectIOEnabled(bool enabled);
    
    /**
     * @brief End the current imaging cycle
     * Emits a CycleEnd event with success/failure status.
     * @param success True if the operation completed successfully
     * @param errorMessage Optional error message (used to determine if cancelled vs failed)
     * 
     * The session state is determined as follows:
     * - success=true → SessionState::Succeeded
     * - success=false, errorMessage contains "Cancel" or "removed" → SessionState::Cancelled
     * - success=false, other error → SessionState::Failed
     */
    void endSession(bool success, const QString &errorMessage = QString());
    
    /**
     * @brief Get the current session state
     */
    SessionState sessionState() const;
    
    /**
     * @brief Reset all captured data (start fresh)
     * Call this when you want to clear all accumulated cycles and start over.
     */
    void reset();
    
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

    /**
     * @brief Add a pre-constructed event directly (for events with explicit timestamps)
     */
    void addEvent(const TimedEvent &event);

    // ===== Lightweight Progress Recording =====
    // These just store raw (timestamp, bytes) pairs - very fast
    
    /**
     * @brief Record download progress (lightweight - just stores raw sample)
     */
    void recordDownloadProgress(quint64 bytesNow, quint64 bytesTotal);
    
    /**
     * @brief Record decompression progress (lightweight - just stores raw sample)
     * This tracks bytes output from the decompressor (uncompressed size)
     */
    void recordDecompressProgress(quint64 bytesDecompressed, quint64 bytesTotal);
    
    /**
     * @brief Record write progress (lightweight - just stores raw sample)
     * This tracks bytes actually written to the storage device
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
     * @brief Check if there is any data available to export
     * Returns true if there are any events or samples (including background events)
     */
    bool hasData() const;
    
    /**
     * @brief Check if there is actual imaging session data to export
     * Returns true only if an imaging session was started (startSession() was called).
     * This distinguishes between having just background events (osListFetch, etc.)
     * and having actual imaging operation data.
     */
    bool hasImagingData() const;
    
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
    
    /**
     * @brief Get session state name as string
     */
    static QString sessionStateName(SessionState state);

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
    SessionState _sessionState;  // Richer state than simple success/fail
    QString _errorMessage;
    
    // System information
    SystemInfo _systemInfo;
    bool _hasSystemInfo;

    // Phase tracking
    Phase _currentPhase;
    qint64 _phaseStartTimes[6];  // Idle, Downloading, Decompressing, Writing, Verifying, Finalising

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
    QVector<RawSample> _decompressSamples;
    QVector<RawSample> _writeSamples;
    QVector<RawSample> _verifySamples;
    
    // Totals for each phase
    quint64 _downloadTotal;
    quint64 _decompressTotal;
    quint64 _writeTotal;
    quint64 _verifyTotal;

    // Rate limiting state
    qint64 _lastSampleTime[4];  // Per-phase last sample time (download, decompress, write, verify)
};

#endif // PERFORMANCESTATS_H
