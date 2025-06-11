/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../buffer_optimization.h"
#include "../config.h"
#include <windows.h>

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
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    size_t pageSize = si.dwPageSize;

    // Start with a reasonable default (1 MiB)
    size_t bufferSize = IMAGEWRITER_BLOCKSIZE;
    
    // For SSD optimization on Windows, larger blocks tend to work better
    // Make sure buffer size is a multiple of page size
    bufferSize = ((bufferSize + pageSize - 1) / pageSize) * pageSize;
    
    // Cap at a reasonable maximum (8 MiB, aligned to page size)
    const size_t maxBufferSize = (((8 * 1024 * 1024) + pageSize - 1) / pageSize) * pageSize;
    if (bufferSize > maxBufferSize)
        bufferSize = maxBufferSize;
        
    return bufferSize;
} 