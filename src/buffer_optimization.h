#ifndef BUFFER_OPTIMIZATION_H
#define BUFFER_OPTIMIZATION_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <cstddef>
#include <QtGlobal>

/**
 * @brief Buffer optimization functions for optimal I/O performance
 * 
 * These functions calculate optimal buffer sizes based on file size,
 * system characteristics, and platform-specific optimizations.
 */

/**
 * @brief Calculate adaptive verification buffer size based on file size
 * @param fileSize Size of file to be verified in bytes
 * @return Optimal buffer size for verification operations
 */
size_t getAdaptiveVerifyBufferSize(qint64 fileSize);

/**
 * @brief Calculate optimal write buffer size based on system characteristics
 * @return Optimal buffer size for write operations
 */
size_t getOptimalWriteBufferSize();

#endif // BUFFER_OPTIMIZATION_H 