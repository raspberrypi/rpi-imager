/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef SYSTEMMEMORYMANAGER_H
#define SYSTEMMEMORYMANAGER_H

#include <QtGlobal>
#include <QString>

/**
 * @brief Platform-agnostic system memory management interface
 * 
 * Provides unified access to system memory information and calculates
 * optimal sync intervals for image writing operations based on available RAM.
 */
class SystemMemoryManager
{
public:
    /**
     * @brief Configuration for adaptive sync intervals
     */
    struct SyncConfiguration {
        qint64 syncIntervalBytes;    // How many bytes to write before syncing
        qint64 syncIntervalMs;       // Maximum time between syncs (milliseconds)
        QString memoryTier;          // Description of memory tier (for logging)
    };

    /**
     * @brief Get the singleton instance
     */
    static SystemMemoryManager& instance();

    /**
     * @brief Get total system memory in MB
     * @return Total system memory in megabytes, or 0 if detection failed
     */
    qint64 getTotalMemoryMB();

    /**
     * @brief Get available system memory in MB
     * @return Available system memory in megabytes, or 0 if detection failed
     */
    qint64 getAvailableMemoryMB();

    /**
     * @brief Calculate optimal sync configuration based on system memory
     * @return SyncConfiguration with adaptive intervals
     */
    SyncConfiguration calculateSyncConfiguration();

    /**
     * @brief Get platform name for logging
     * @return Platform identifier string
     */
    QString getPlatformName();

    /**
     * @brief Calculate optimal write buffer size based on available memory
     * @return Optimal buffer size for write operations in bytes
     */
    size_t getOptimalWriteBufferSize();

    /**
     * @brief Calculate adaptive verification buffer size based on file size and available memory
     * @param fileSize Size of file to be verified in bytes
     * @return Optimal buffer size for verification operations in bytes
     */
    size_t getAdaptiveVerifyBufferSize(qint64 fileSize);

    /**
     * @brief Calculate optimal input buffer size for downloads/streams
     * @return Optimal buffer size for input operations in bytes
     */
    size_t getOptimalInputBufferSize();

    /**
     * @brief Get system page size for memory alignment
     * @return System page size in bytes
     */
    size_t getSystemPageSize();

    /**
     * @brief Calculate optimal ring buffer slot count based on available memory
     * 
     * Balances pipelining depth against memory usage. More slots allow
     * better overlap between producer and consumer, but use more memory.
     * 
     * @param slotSize Size of each slot in bytes
     * @return Optimal number of slots for the ring buffer
     */
    size_t getOptimalRingBufferSlots(size_t slotSize);

    /**
     * @brief Log a summary of all memory-based configuration
     * 
     * Useful for diagnostics - shows all adaptive settings based on detected memory.
     */
    void logConfigurationSummary();

private:
    SystemMemoryManager() = default;
    ~SystemMemoryManager() = default;
    SystemMemoryManager(const SystemMemoryManager&) = delete;
    SystemMemoryManager& operator=(const SystemMemoryManager&) = delete;

    // Platform-specific implementations
    qint64 getPlatformTotalMemoryMB();
    qint64 getPlatformAvailableMemoryMB();

    // Configuration constants
    static constexpr qint64 MIN_SYNC_INTERVAL_BYTES = 16LL * 1024 * 1024;   // 16MB minimum
    static constexpr qint64 MAX_SYNC_INTERVAL_BYTES = 256LL * 1024 * 1024;  // 256MB maximum
    static constexpr qint64 DEFAULT_SYNC_INTERVAL_MS = 5000;                // 5 seconds default
    
    // Memory tier thresholds
    static constexpr qint64 LOW_MEMORY_THRESHOLD_MB = 2048;     // 2GB
    static constexpr qint64 HIGH_MEMORY_THRESHOLD_MB = 8192;    // 8GB

    // Cached values to avoid repeated system calls
    mutable qint64 _cachedTotalMemoryMB = -1;
    mutable qint64 _cachedAvailableMemoryMB = -1;
};

#endif // SYSTEMMEMORYMANAGER_H
