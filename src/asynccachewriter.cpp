/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "asynccachewriter.h"
#include <QDebug>
#include <QFileInfo>

AsyncCacheWriter::AsyncCacheWriter(QObject *parent)
    : QThread(parent)
    , _maxQueueSize(32)
    , _maxQueueMemory(64 * 1024 * 1024)
    , _hash(OSLIST_HASH_ALGORITHM)
    , _isActive(false)
    , _shouldStop(false)
    , _hasError(false)
    , _finishing(false)
    , _bytesQueued(0)
    , _bytesWritten(0)
{
    _initializeQueueLimits();
}

void AsyncCacheWriter::_initializeQueueLimits()
{
    qint64 totalMemMB = SystemMemoryManager::instance().getTotalMemoryMB();
    
    // Adaptive queue sizing based on system memory
    // Cache writing is less critical than main I/O, so we use conservative memory allocation
    if (totalMemMB < 1024) {
        // Very low memory (< 1GB): Minimal caching to preserve RAM
        _maxQueueSize = 8;
        _maxQueueMemory = 8 * 1024 * 1024;  // 8MB max
    } else if (totalMemMB < 2048) {
        // Low memory (1-2GB): Small cache buffer
        _maxQueueSize = 16;
        _maxQueueMemory = 16 * 1024 * 1024;  // 16MB max
    } else if (totalMemMB < 4096) {
        // Medium memory (2-4GB): Moderate cache buffer
        _maxQueueSize = 24;
        _maxQueueMemory = 32 * 1024 * 1024;  // 32MB max
    } else if (totalMemMB < 8192) {
        // High memory (4-8GB): Comfortable cache buffer
        _maxQueueSize = 32;
        _maxQueueMemory = 64 * 1024 * 1024;  // 64MB max
    } else {
        // Very high memory (> 8GB): Large cache buffer for best performance
        _maxQueueSize = 48;
        _maxQueueMemory = 128 * 1024 * 1024;  // 128MB max
    }
    
    qDebug() << "AsyncCacheWriter: Queue limits set to" << _maxQueueSize << "chunks,"
             << (_maxQueueMemory / (1024 * 1024)) << "MB for" << totalMemMB << "MB system";
}

AsyncCacheWriter::~AsyncCacheWriter()
{
    cancel();
}

bool AsyncCacheWriter::open(const QString &filename, qint64 preallocateSize)
{
    if (_isActive) {
        qDebug() << "AsyncCacheWriter: Already active";
        return false;
    }
    
    _filename = filename;
    _file.setFileName(filename);
    
    if (!_file.open(QIODevice::WriteOnly)) {
        qDebug() << "AsyncCacheWriter: Failed to open" << filename << "-" << _file.errorString();
        return false;
    }
    
    if (preallocateSize > 0) {
        // Pre-allocate space to avoid fragmentation
        if (!_file.resize(preallocateSize)) {
            qDebug() << "AsyncCacheWriter: Failed to pre-allocate" << preallocateSize << "bytes";
            // Continue anyway, not fatal
        }
        // Seek back to beginning
        _file.seek(0);
    }
    
    // Reset state
    _shouldStop = false;
    _hasError = false;
    _finishing = false;
    _bytesQueued = 0;
    _bytesWritten = 0;
    _isActive = true;
    
    // Reset hash for fresh computation
    _hash.reset();
    
    // Clear any stale queue data
    {
        QMutexLocker lock(&_mutex);
        _queue.clear();
    }
    
    // Start the writer thread
    start();
    
    qDebug() << "AsyncCacheWriter: Opened" << filename 
             << "with preallocation:" << preallocateSize;
    return true;
}

bool AsyncCacheWriter::write(const char *data, size_t len)
{
    if (!_isActive || _hasError || _shouldStop) {
        return false;
    }
    
    {
        QMutexLocker lock(&_mutex);
        
        // Check if queue is full - use non-blocking approach with graceful degradation
        // If queue is full, we have two options:
        // 1. Brief wait (up to 500ms) to see if space becomes available
        // 2. If still full, disable caching rather than blocking the download
        
        if (_queue.size() >= _maxQueueSize || 
            queueMemoryUsage() >= _maxQueueMemory) {
            
            // Try brief wait for space (don't block download for too long)
            static constexpr int MAX_BACKPRESSURE_WAIT_MS = 500;
            static constexpr int WAIT_INTERVAL_MS = 50;
            int waitedMs = 0;
            
            while ((_queue.size() >= _maxQueueSize || 
                    queueMemoryUsage() >= _maxQueueMemory) &&
                   waitedMs < MAX_BACKPRESSURE_WAIT_MS) {
                
                if (_shouldStop || _hasError) {
                    return false;
                }
                
                _queueNotFull.wait(&_mutex, WAIT_INTERVAL_MS);
                waitedMs += WAIT_INTERVAL_MS;
            }
            
            // If still full after waiting, cache I/O is too slow - disable caching
            if (_queue.size() >= _maxQueueSize || 
                queueMemoryUsage() >= _maxQueueMemory) {
                qDebug() << "AsyncCacheWriter: Queue still full after" << waitedMs 
                         << "ms wait. Cache I/O too slow, disabling caching to avoid blocking download.";
                _hasError = true;  // Signal error state
                return false;
            }
        }
        
        // Create a copy of the data for async processing
        WriteChunk chunk;
        chunk.data = QByteArray(data, static_cast<int>(len));
        
        _queue.enqueue(std::move(chunk));
        _bytesQueued += len;
    }
    
    // Signal the writer thread that data is available
    _queueNotEmpty.wakeOne();
    
    return true;
}

void AsyncCacheWriter::finish()
{
    if (!_isActive) {
        return;
    }
    
    qDebug() << "AsyncCacheWriter: Finishing - waiting for" 
             << (_bytesQueued - _bytesWritten) << "bytes to be written";
    
    _finishing = true;
    
    // Wake the writer thread to process remaining data
    _queueNotEmpty.wakeAll();
    
    // Wait for thread to complete
    wait();
    
    if (!_hasError) {
        // Flush and close the file
        _file.flush();
        _file.close();
        
        qDebug() << "AsyncCacheWriter: Finished successfully, wrote" 
                 << _bytesWritten << "bytes";
        
        emit finished(_hash.result().toHex());
    }
    
    _isActive = false;
}

void AsyncCacheWriter::cancel()
{
    if (!_isActive && !isRunning()) {
        return;
    }
    
    qDebug() << "AsyncCacheWriter: Cancelling";
    
    _shouldStop = true;
    
    // Wake the thread so it can exit
    _queueNotEmpty.wakeAll();
    _queueNotFull.wakeAll();
    
    // Wait for thread to finish
    if (!wait(5000)) {
        qDebug() << "AsyncCacheWriter: Thread didn't stop in time, terminating";
        terminate();
        wait();
    }
    
    cleanup();
    _isActive = false;
}

void AsyncCacheWriter::run()
{
    qDebug() << "AsyncCacheWriter: Thread started";
    
    while (!_shouldStop) {
        WriteChunk chunk;
        bool hasData = false;
        
        {
            QMutexLocker lock(&_mutex);
            
            // Wait for data or stop signal
            while (_queue.isEmpty() && !_shouldStop && !_finishing) {
                _queueNotEmpty.wait(&_mutex, 100);
            }
            
            if (!_queue.isEmpty()) {
                chunk = _queue.dequeue();
                hasData = true;
            }
        }
        
        // Signal that queue has space
        _queueNotFull.wakeOne();
        
        if (hasData) {
            // Compute hash of the data
            _hash.addData(chunk.data);
            
            // Write to file
            qint64 written = _file.write(chunk.data);
            if (written != chunk.data.size()) {
                qDebug() << "AsyncCacheWriter: Write error -" << _file.errorString();
                _hasError = true;
                emit error(tr("Cache write error: %1").arg(_file.errorString()));
                break;
            }
            
            _bytesWritten += written;
        }
        
        // Check if we're done (finishing and queue empty)
        {
            QMutexLocker lock(&_mutex);
            if (_finishing && _queue.isEmpty()) {
                break;
            }
        }
    }
    
    // If cancelled or error, clean up
    if (_shouldStop || _hasError) {
        cleanup();
    }
    
    qDebug() << "AsyncCacheWriter: Thread finished, wrote" << _bytesWritten << "bytes";
}

void AsyncCacheWriter::cleanup()
{
    // Clear the queue
    {
        QMutexLocker lock(&_mutex);
        _queue.clear();
    }
    
    // Close and remove the cache file
    if (_file.isOpen()) {
        _file.close();
    }
    
    if (!_filename.isEmpty() && QFileInfo::exists(_filename)) {
        QFile::remove(_filename);
        qDebug() << "AsyncCacheWriter: Removed cache file" << _filename;
    }
}

QByteArray AsyncCacheWriter::hash() const
{
    return _hash.result().toHex();
}

qint64 AsyncCacheWriter::queueMemoryUsage() const
{
    qint64 total = 0;
    for (const auto &chunk : _queue) {
        total += chunk.data.size();
    }
    return total;
}

