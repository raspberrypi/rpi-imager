/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "systemmemorymanager.h"
#include "config.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

// Platform-specific includes
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_DARWIN)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <unistd.h>
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

SystemMemoryManager& SystemMemoryManager::instance()
{
    static SystemMemoryManager instance;
    return instance;
}

qint64 SystemMemoryManager::getTotalMemoryMB()
{
    if (_cachedTotalMemoryMB == -1) {
        _cachedTotalMemoryMB = getPlatformTotalMemoryMB();
        if (_cachedTotalMemoryMB <= 0) {
            qDebug() << "Warning: Could not detect system memory, assuming 4GB";
            _cachedTotalMemoryMB = 4096;
        }
        qDebug() << "Detected total system memory:" << _cachedTotalMemoryMB << "MB on" << getPlatformName();
    }
    return _cachedTotalMemoryMB;
}

qint64 SystemMemoryManager::getAvailableMemoryMB()
{
    // Use cached value if available (cache for short period to avoid constant syscalls)
    if (_cachedAvailableMemoryMB > 0) {
        return _cachedAvailableMemoryMB;
    }
    
    qint64 availableMB = getPlatformAvailableMemoryMB();
    
    // Fall back to total memory if platform detection failed
    if (availableMB <= 0) {
        availableMB = getTotalMemoryMB();
    }
    
    _cachedAvailableMemoryMB = availableMB;
    return availableMB;
}

SystemMemoryManager::SyncConfiguration SystemMemoryManager::calculateSyncConfiguration()
{
    qint64 totalMemMB = getTotalMemoryMB();
    SyncConfiguration config;
    
    qint64 syncIntervalMB;
    
    if (totalMemMB < LOW_MEMORY_THRESHOLD_MB) {
        // Low memory: aggressive syncing to prevent OOM
        syncIntervalMB = qMax(16LL, totalMemMB / 64);  // ~1.5% of RAM, min 16MB
        config.syncIntervalMs = 3000;  // More frequent time-based syncs (3 seconds)
        config.memoryTier = QString("Low memory (%1MB)").arg(totalMemMB);
    } else if (totalMemMB < HIGH_MEMORY_THRESHOLD_MB) {
        // Medium memory: balanced approach
        syncIntervalMB = qMax(32LL, totalMemMB / 80);  // ~1.25% of RAM, min 32MB
        config.syncIntervalMs = DEFAULT_SYNC_INTERVAL_MS;  // Standard 5 seconds
        config.memoryTier = QString("Medium memory (%1MB)").arg(totalMemMB);
    } else {
        // High memory: conservative syncing for better performance
        syncIntervalMB = qMin(256LL, qMax(64LL, totalMemMB / 64));  // ~1.5% of RAM, capped at 256MB
        config.syncIntervalMs = 7000;  // Less frequent time-based syncs (7 seconds)
        config.memoryTier = QString("High memory (%1MB)").arg(totalMemMB);
    }
    
    // Convert to bytes and apply bounds
    config.syncIntervalBytes = syncIntervalMB * 1024 * 1024;
    config.syncIntervalBytes = qMax(MIN_SYNC_INTERVAL_BYTES, 
                                   qMin(MAX_SYNC_INTERVAL_BYTES, config.syncIntervalBytes));
    
    qDebug() << "Adaptive sync configuration:"
             << config.memoryTier
             << "- Sync interval:" << (config.syncIntervalBytes / 1024 / 1024) << "MB"
             << "- Time interval:" << config.syncIntervalMs << "ms"
             << "- Platform:" << getPlatformName();
    
    return config;
}

QString SystemMemoryManager::getPlatformName()
{
#ifdef Q_OS_WIN
    return "Windows";
#elif defined(Q_OS_DARWIN)
    return "macOS";
#elif defined(Q_OS_LINUX)
    return "Linux";
#else
    return "Unknown";
#endif
}

// Platform-specific implementations

#ifdef Q_OS_WIN
qint64 SystemMemoryManager::getPlatformTotalMemoryMB()
{
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    
    if (GlobalMemoryStatusEx(&memStatus)) {
        // Convert bytes to MB
        return static_cast<qint64>(memStatus.ullTotalPhys / (1024 * 1024));
    }
    
    return 0; // Detection failed
}

qint64 SystemMemoryManager::getPlatformAvailableMemoryMB()
{
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    
    if (GlobalMemoryStatusEx(&memStatus)) {
        // Convert bytes to MB
        return static_cast<qint64>(memStatus.ullAvailPhys / (1024 * 1024));
    }
    
    return 0; // Detection failed
}

#elif defined(Q_OS_DARWIN)
qint64 SystemMemoryManager::getPlatformTotalMemoryMB()
{
    int64_t memsize = 0;
    size_t size = sizeof(memsize);
    
    if (sysctlbyname("hw.memsize", &memsize, &size, NULL, 0) == 0) {
        // Convert bytes to MB
        return static_cast<qint64>(memsize / (1024 * 1024));
    }
    
    return 0; // Detection failed
}

qint64 SystemMemoryManager::getPlatformAvailableMemoryMB()
{
    vm_size_t page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t host_size = sizeof(vm_stat) / sizeof(natural_t);
    
    if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO64, 
                         (host_info64_t)&vm_stat, &host_size) == KERN_SUCCESS) {
        
        // Calculate available memory (free + inactive + cached)
        int64_t available_pages = vm_stat.free_count + vm_stat.inactive_count;
        int64_t available_bytes = available_pages * page_size;
        
        return static_cast<qint64>(available_bytes / (1024 * 1024));
    }
    
    return 0; // Detection failed
}

#elif defined(Q_OS_LINUX)
qint64 SystemMemoryManager::getPlatformTotalMemoryMB()
{
    // Method 1: Try sysinfo first (more reliable)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        // Convert to MB
        return static_cast<qint64>(info.totalram * info.mem_unit / (1024 * 1024));
    }
    
    // Method 2: Fallback to /proc/meminfo
    QFile meminfo("/proc/meminfo");
    if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&meminfo);
        QString line;
        while (!(line = in.readLine()).isNull()) {
            if (line.startsWith("MemTotal:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    bool ok;
                    qint64 memKB = parts[1].toLongLong(&ok);
                    if (ok) {
                        return memKB / 1024; // Convert KB to MB
                    }
                }
                break;
            }
        }
        meminfo.close();
    }
    
    return 0; // Detection failed
}

qint64 SystemMemoryManager::getPlatformAvailableMemoryMB()
{
    // Method 1: Try sysinfo first
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        // Available = free + buffers + cached (approximation)
        qint64 available = (info.freeram + info.bufferram) * info.mem_unit;
        return static_cast<qint64>(available / (1024 * 1024));
    }
    
    // Method 2: Parse /proc/meminfo for more detailed info
    QFile meminfo("/proc/meminfo");
    if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&meminfo);
        QString line;
        qint64 memAvailableKB = 0, memFreeKB = 0, buffersKB = 0, cachedKB = 0;
        
        while (!(line = in.readLine()).isNull()) {
            if (line.startsWith("MemAvailable:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    memAvailableKB = parts[1].toLongLong();
                }
            } else if (line.startsWith("MemFree:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    memFreeKB = parts[1].toLongLong();
                }
            } else if (line.startsWith("Buffers:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    buffersKB = parts[1].toLongLong();
                }
            } else if (line.startsWith("Cached:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    cachedKB = parts[1].toLongLong();
                }
            }
        }
        meminfo.close();
        
        // Use MemAvailable if available (kernel 3.14+), otherwise estimate
        if (memAvailableKB > 0) {
            return memAvailableKB / 1024;
        } else if (memFreeKB > 0) {
            // Rough estimate: free + buffers + cached
            return (memFreeKB + buffersKB + cachedKB) / 1024;
        }
    }
    
    return 0; // Detection failed
}

#else
// Fallback for unknown platforms
qint64 SystemMemoryManager::getPlatformTotalMemoryMB()
{
    return 0; // Will trigger fallback to 4GB assumption
}

qint64 SystemMemoryManager::getPlatformAvailableMemoryMB()
{
    return 0; // Will use total memory
}
#endif

// Memory-aware buffer sizing implementations

size_t SystemMemoryManager::getOptimalWriteBufferSize()
{
    qint64 totalMemMB = getTotalMemoryMB();
    size_t pageSize = getSystemPageSize();
    
    // Base buffer size calculation based on available memory
    size_t bufferSize;
    
    if (totalMemMB < 1024) {
        // Very low memory (< 1GB): Conservative 512KB
        bufferSize = 512 * 1024;
    } else if (totalMemMB < 2048) {
        // Low memory (1-2GB): 1MB default
        bufferSize = IMAGEWRITER_BLOCKSIZE;
    } else if (totalMemMB < 4096) {
        // Medium memory (2-4GB): 2MB for better throughput
        bufferSize = 2 * 1024 * 1024;
    } else if (totalMemMB < 8192) {
        // High memory (4-8GB): 4MB
        bufferSize = 4 * 1024 * 1024;
    } else {
        // Very high memory (> 8GB): 8MB for maximum throughput
        bufferSize = 8 * 1024 * 1024;
    }
    
    // Align to page boundaries for optimal performance
    bufferSize = ((bufferSize + pageSize - 1) / pageSize) * pageSize;
    
    // Apply platform-specific constraints
    const size_t minBufferSize = 256 * 1024;  // 256KB minimum
    const size_t maxBufferSize = 16 * 1024 * 1024;  // 16MB maximum
    bufferSize = qMax(minBufferSize, qMin(maxBufferSize, bufferSize));
    
    qDebug() << "Optimal write buffer size:" << (bufferSize / 1024) << "KB for" 
             << totalMemMB << "MB system";
    
    return bufferSize;
}

size_t SystemMemoryManager::getAdaptiveVerifyBufferSize(qint64 fileSize)
{
    qint64 totalMemMB = getTotalMemoryMB();
    
    // Base verification buffer size based on file size
    size_t baseBufferSize;
    if (fileSize < 100LL * 1024 * 1024) {        // < 100MB: use 256KB
        baseBufferSize = 256 * 1024;
    } else if (fileSize < 1024LL * 1024 * 1024) { // 100MB-1GB: use 2MB  
        baseBufferSize = 2 * 1024 * 1024;
    } else if (fileSize < 4LL * 1024 * 1024 * 1024) { // 1GB-4GB: use 4MB
        baseBufferSize = 4 * 1024 * 1024;
    } else {                                       // > 4GB: use 8MB
        baseBufferSize = 8 * 1024 * 1024;
    }
    
    // Adjust based on available system memory
    size_t memoryAdjustedSize = baseBufferSize;
    
    if (totalMemMB < 1024) {
        // Very low memory: reduce buffer sizes by 50%
        memoryAdjustedSize = baseBufferSize / 2;
    } else if (totalMemMB < 2048) {
        // Low memory: reduce buffer sizes by 25%
        memoryAdjustedSize = (baseBufferSize * 3) / 4;
    } else if (totalMemMB > 8192) {
        // High memory: can afford larger buffers (up to 50% larger)
        memoryAdjustedSize = qMin(baseBufferSize * 3 / 2, static_cast<size_t>(16 * 1024 * 1024));
    }
    
    // Apply bounds
    const size_t minVerifyBufferSize = 128 * 1024;  // 128KB minimum
    const size_t maxVerifyBufferSize = 16 * 1024 * 1024;  // 16MB maximum
    memoryAdjustedSize = qMax(minVerifyBufferSize, qMin(maxVerifyBufferSize, memoryAdjustedSize));
    
    // Align to page boundaries
    size_t pageSize = getSystemPageSize();
    memoryAdjustedSize = ((memoryAdjustedSize + pageSize - 1) / pageSize) * pageSize;
    
    return memoryAdjustedSize;
}

size_t SystemMemoryManager::getOptimalInputBufferSize()
{
    qint64 totalMemMB = getTotalMemoryMB();
    
    // Input buffer (ring buffer slot size) for downloads/streams.
    // Larger buffers reduce per-chunk overhead and improve throughput,
    // especially for decompression which works more efficiently on larger blocks.
    // The ring buffer will have multiple slots of this size.
    //
    // For high-speed scenarios (10G + NVMe), we want large slots to:
    // - Reduce lock contention between producer/consumer
    // - Allow decompressor to work on larger blocks efficiently
    // - Reduce syscall overhead for network reads
    size_t inputBufferSize;
    
    if (totalMemMB < 1024) {
        // Very low memory: 128KB
        inputBufferSize = 128 * 1024;
    } else if (totalMemMB < 2048) {
        // Low memory: 256KB
        inputBufferSize = 256 * 1024;
    } else if (totalMemMB < 4096) {
        // Medium memory: 512KB
        inputBufferSize = 512 * 1024;
    } else if (totalMemMB < 8192) {
        // High memory: 1MB - good for Gbps networks
        inputBufferSize = 1024 * 1024;
    } else if (totalMemMB < 16384) {
        // Very high memory: 2MB - optimal for fast SSDs
        inputBufferSize = 2 * 1024 * 1024;
    } else {
        // Extreme memory (16GB+): 4MB - optimal for 10G + NVMe
        // Large slots reduce producer/consumer handoff overhead at high speeds
        inputBufferSize = 4 * 1024 * 1024;
    }
    
    // Align to page boundaries
    size_t pageSize = getSystemPageSize();
    inputBufferSize = ((inputBufferSize + pageSize - 1) / pageSize) * pageSize;
    
    return inputBufferSize;
}

size_t SystemMemoryManager::getSystemPageSize()
{
#ifdef Q_OS_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#elif defined(Q_OS_DARWIN) || defined(Q_OS_LINUX)
    size_t pageSize = sysconf(_SC_PAGESIZE);
    return (pageSize == (size_t)-1) ? 4096 : pageSize;
#else
    return 4096; // Fallback to common page size
#endif
}

size_t SystemMemoryManager::getOptimalRingBufferSlots(size_t slotSize)
{
    qint64 totalMemMB = getTotalMemoryMB();
    qint64 availableMemMB = getAvailableMemoryMB();
    
    // AGGRESSIVE STRATEGY: Maximize throughput on ALL systems.
    // 
    // Users run Imager as their primary task - they're waiting for it to complete.
    // Use available RAM aggressively regardless of system size.
    //
    // Strategy:
    // - Use 30% of AVAILABLE RAM (adapts to actual system state)
    // - Minimum 2 MB (baseline for any system)
    // - Maximum 16 GB (sanity cap)
    //
    // For 10G + NVMe with 500 MB/s burst accumulation:
    // - 1 GB buffer = 2 seconds of absorption
    // - 5 GB buffer = 10 seconds of absorption
    
    // Calculate target: 30% of available RAM
    size_t targetFromAvailable = static_cast<size_t>(availableMemMB) * 1024 * 1024 * 3 / 10;
    
    // Minimum: 2 MB (ensures basic functionality)
    const size_t minimumBuffer = 2 * 1024 * 1024;
    
    // Maximum: 16 GB (sanity cap)
    const size_t maximumBuffer = static_cast<size_t>(16) * 1024 * 1024 * 1024;
    
    // Apply bounds
    size_t targetMemory = qMax(minimumBuffer, qMin(maximumBuffer, targetFromAvailable));
    
    // Calculate slots based on target memory and slot size
    size_t calculatedSlots = targetMemory / slotSize;
    
    // Ensure reasonable slot counts (at least 8 for pipelining, at most 8192)
    calculatedSlots = qMax(static_cast<size_t>(8), qMin(static_cast<size_t>(8192), calculatedSlots));
    
    size_t actualBufferMB = (calculatedSlots * slotSize) / (1024 * 1024);
    qDebug() << "Ring buffer:" << calculatedSlots << "slots x" << (slotSize / 1024) << "KB ="
             << actualBufferMB << "MB"
             << "(available:" << availableMemMB << "MB, total:" << totalMemMB << "MB)";
    
    return calculatedSlots;
}

void SystemMemoryManager::logConfigurationSummary()
{
    qint64 totalMemMB = getTotalMemoryMB();
    SyncConfiguration syncConfig = calculateSyncConfiguration();
    size_t inputBuf = getOptimalInputBufferSize();
    size_t writeBuf = getOptimalWriteBufferSize();
    
    qDebug() << "=== Memory-Adaptive Configuration Summary ===";
    qDebug() << "Platform:" << getPlatformName();
    qDebug() << "System Memory:" << totalMemMB << "MB";
    qDebug() << "Memory Tier:" << syncConfig.memoryTier;
    qDebug() << "Input Buffer:" << (inputBuf / 1024) << "KB";
    qDebug() << "Write Buffer:" << (writeBuf / 1024) << "KB";
    qDebug() << "Sync Interval:" << (syncConfig.syncIntervalBytes / (1024 * 1024)) << "MB /" 
             << syncConfig.syncIntervalMs << "ms";
    qDebug() << "=============================================";
}

