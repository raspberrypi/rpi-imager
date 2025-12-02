/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef ASYNCCACHEWRITER_H
#define ASYNCCACHEWRITER_H

#include <QThread>
#include <QFile>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QByteArray>
#include <atomic>
#include <functional>
#include "acceleratedcryptographichash.h"
#include "config.h"
#include "systemmemorymanager.h"

/**
 * @brief Asynchronous cache file writer
 * 
 * This class provides non-blocking cache file writes by running I/O
 * in a dedicated background thread. Data is queued and written
 * asynchronously, allowing the download to continue without waiting
 * for cache writes to complete.
 */
class AsyncCacheWriter : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param parent Parent QObject
     */
    explicit AsyncCacheWriter(QObject *parent = nullptr);
    
    /**
     * @brief Destructor - waits for thread to finish
     */
    ~AsyncCacheWriter() override;

    /**
     * @brief Open cache file for writing
     * @param filename Path to cache file
     * @param preallocateSize Size to pre-allocate (0 = no preallocation)
     * @return true if file opened successfully
     */
    bool open(const QString &filename, qint64 preallocateSize = 0);

    /**
     * @brief Queue data for async writing
     * 
     * This method returns quickly after queuing the data.
     * 
     * Backpressure handling:
     * - If the queue is full, waits briefly (up to 500ms) for space
     * - If still full after waiting, caching is disabled to avoid
     *   blocking the download (returns false, sets error state)
     * - The download continues without caching in this case
     * 
     * @param data Pointer to data buffer
     * @param len Length of data
     * @return true if data was queued, false if writer is in error state
     *         or caching was disabled due to slow I/O
     */
    bool write(const char *data, size_t len);

    /**
     * @brief Flush all pending writes and close the file
     * 
     * Blocks until all queued writes are complete.
     */
    void finish();

    /**
     * @brief Cancel writing and discard pending data
     * 
     * Stops the writer thread and removes the cache file.
     */
    void cancel();

    /**
     * @brief Check if writer is in error state
     * @return true if an error occurred (including backpressure timeout)
     */
    bool hasError() const { return _hasError; }

    /**
     * @brief Check if caching was disabled due to slow I/O
     * 
     * When the cache disk is too slow to keep up with the download,
     * caching is automatically disabled after a brief backpressure wait
     * to avoid blocking the main download. This allows the download to
     * complete successfully without caching.
     * 
     * @return true if caching was disabled due to backpressure
     */
    bool wasDisabledDueToBackpressure() const { return _hasError && _isActive; }

    /**
     * @brief Get the computed hash of all written data
     * 
     * Only valid after finish() has been called.
     * @return SHA256 hash in hex format
     */
    QByteArray hash() const;

    /**
     * @brief Check if cache writing is enabled and active
     * @return true if actively writing to cache
     */
    bool isActive() const { return _isActive && !_hasError; }

signals:
    /**
     * @brief Emitted when an error occurs during cache writing
     * @param message Error description
     */
    void error(const QString &message);

    /**
     * @brief Emitted when cache writing completes successfully
     * @param hash SHA256 hash of the cache file contents
     */
    void finished(const QByteArray &hash);

protected:
    void run() override;

private:
    // Queue management - initialized based on system memory
    int _maxQueueSize;       // Max pending write chunks
    qint64 _maxQueueMemory;  // Max memory in queue (bytes)
    
    struct WriteChunk {
        QByteArray data;
    };
    
    QQueue<WriteChunk> _queue;
    
    // Initialize adaptive queue limits based on available system memory
    void _initializeQueueLimits();
    QMutex _mutex;
    QWaitCondition _queueNotEmpty;
    QWaitCondition _queueNotFull;
    
    // File state
    QFile _file;
    QString _filename;
    
    // Hash computation
    AcceleratedCryptographicHash _hash;
    
    // Control flags
    std::atomic<bool> _isActive;
    std::atomic<bool> _shouldStop;
    std::atomic<bool> _hasError;
    std::atomic<bool> _finishing;
    
    // Statistics
    std::atomic<qint64> _bytesQueued;
    std::atomic<qint64> _bytesWritten;
    
    // Helper methods
    void processQueue();
    void cleanup();
    qint64 queueMemoryUsage() const;
};

#endif // ASYNCCACHEWRITER_H

