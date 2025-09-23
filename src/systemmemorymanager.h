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

private:
    SystemMemoryManager() = default;
    ~SystemMemoryManager() = default;
    SystemMemoryManager(const SystemMemoryManager&) = delete;
    SystemMemoryManager& operator=(const SystemMemoryManager&) = delete;

    // Platform-specific implementations
    qint64 getPlatformTotalMemoryMB();
    qint64 getPlatformAvailableMemoryMB();

    // Configuration constants
    static const qint64 MIN_SYNC_INTERVAL_BYTES = 16 * 1024 * 1024;   // 16MB minimum
    static const qint64 MAX_SYNC_INTERVAL_BYTES = 256 * 1024 * 1024;  // 256MB maximum
    static const qint64 DEFAULT_SYNC_INTERVAL_MS = 5000;              // 5 seconds default
    
    // Memory tier thresholds
    static const qint64 LOW_MEMORY_THRESHOLD_MB = 2048;     // 2GB
    static const qint64 HIGH_MEMORY_THRESHOLD_MB = 8192;    // 8GB

    // Cached values to avoid repeated system calls
    mutable qint64 _cachedTotalMemoryMB = -1;
    mutable qint64 _cachedAvailableMemoryMB = -1;
};

#endif // SYSTEMMEMORYMANAGER_H
