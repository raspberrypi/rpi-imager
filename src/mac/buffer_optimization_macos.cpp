/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../buffer_optimization.h"
#include "../config.h"
#include <unistd.h>
#include <sys/sysctl.h>

size_t getAdaptiveVerifyBufferSize(qint64 fileSize)
{
    if (fileSize < 100LL * 1024 * 1024) {        // < 100MB: use 256KB
        return 256 * 1024;
    } else if (fileSize < 1024LL * 1024 * 1024) { // 100MB-1GB: use 2MB  
        return 2 * 1024 * 1024;
    } else if (fileSize < 4LL * 1024 * 1024 * 1024) { // 1GB-4GB: use 4MB
        return 4 * 1024 * 1024;
    } else {                                       // > 4GB: use 8MB
        return 8 * 1024 * 1024;
    }
}

size_t getOptimalWriteBufferSize()
{
    size_t pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize == (size_t)-1) {
        pageSize = 4096; // Fallback to common page size
    }

    // Start with a reasonable default (1 MiB)
    size_t bufferSize = IMAGEWRITER_BLOCKSIZE;
    
    // For macOS I/O optimization, consider both page alignment and disk characteristics
    // macOS has sophisticated caching, so larger buffers can be beneficial
    bufferSize = ((bufferSize + pageSize - 1) / pageSize) * pageSize;
    
    // On macOS, we can be a bit more aggressive with buffer sizes for external storage
    // Cap at a reasonable maximum (8 MiB, aligned to page size)
    const size_t maxBufferSize = (((8 * 1024 * 1024) + pageSize - 1) / pageSize) * pageSize;
    if (bufferSize > maxBufferSize)
        bufferSize = maxBufferSize;
        
    return bufferSize;
} 