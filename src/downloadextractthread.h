#ifndef DOWNLOADEXTRACTTHREAD_H
#define DOWNLOADEXTRACTTHREAD_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadthread.h"
#include "ringbuffer.h"
#include <condition_variable>
#include <QFuture>
#include <memory>

class _extractThreadClass;

class DownloadExtractThread : public DownloadThread
{
    Q_OBJECT
public:
    /*
     * Constructor
     *
     * - url: URL to download
     * - localfolder: Folder to extract archive to
     */
    explicit DownloadExtractThread(const QByteArray &url, const QByteArray &localfilename = "", const QByteArray &expectedHash = "", QObject *parent = nullptr);

    virtual ~DownloadExtractThread();
    virtual void cancelDownload();
    virtual void extractImageRun();
    virtual void extractMultiFileRun();
    virtual bool isImage();
    virtual void enableMultipleFileExtraction();

signals:
    void downloadProgressChanged(quint64 now, quint64 total);
    void decompressProgressChanged(quint64 now, quint64 total);
    void writeProgressChanged(quint64 now, quint64 total);
    void verifyProgressChanged(quint64 now, quint64 total);
    void eventRingBufferStats(qint64 timestampMs, quint32 durationMs, QString metadata);  // Ring buffer stall event
    
    // Pipeline timing summary events (emitted at end of extraction)
    void eventPipelineDecompressionTime(quint32 totalMs, quint64 bytesDecompressed);
    void eventPipelineWriteWaitTime(quint32 totalMs, quint64 bytesWritten);
    void eventPipelineRingBufferWaitTime(quint32 totalMs, quint64 bytesRead);
    void eventWriteRingBufferStats(quint64 producerStalls, quint64 consumerStalls, 
                                   quint64 producerWaitMs, quint64 consumerWaitMs);

protected:
    size_t _writeBufferSize;
    _extractThreadClass *_extractThread;
    
    // Zero-copy ring buffer for curl -> libarchive data transfer (compressed data)
    std::unique_ptr<RingBuffer> _ringBuffer;
    static const int RING_BUFFER_SLOTS;  // Number of slots in ring buffer
    RingBuffer::Slot* _currentReadSlot;  // Current slot being read by libarchive
    
    // Ring buffer for decompress -> write path (decompressed data)
    // Uses 4 slots to ensure buffers aren't reused while hash computation is pending
    std::unique_ptr<RingBuffer> _writeRingBuffer;
    RingBuffer::Slot* _currentWriteSlot;  // Current slot being written
    
    bool _ethreadStarted, _isImage;
    AcceleratedCryptographicHash _inputHash;
    bool _writeThreadStarted;
    QFuture<size_t> _writeFuture;
    bool _progressStarted;
    qint64 _lastProgressTime;
    quint64 _lastEmittedDlNow, _lastLocalVerifyNow;
    quint64 _lastEmittedDecompressNow, _lastEmittedWriteNow;
    std::atomic<quint64> _bytesDecompressed;  // Total bytes output from decompressor
    bool _downloadComplete;
    QElapsedTimer _sessionTimer;  // Timer for stall event timestamps
    
    // Pipeline timing accumulators (for performance analysis)
    std::atomic<quint64> _totalDecompressionMs;   // Time spent in archive_read_data()
    std::atomic<quint64> _totalWriteWaitMs;       // Time blocked waiting for _writeFuture.result()
    std::atomic<quint64> _totalRingBufferWaitMs;  // Time in _on_read() waiting for data
    std::atomic<quint64> _bytesReadFromRingBuffer;// Bytes read from ring buffer

    void _pushQueue(const char *data, size_t len);
    void _cancelExtract();
    virtual size_t _writeData(const char *buf, size_t len);
    virtual void _onDownloadSuccess();
    virtual void _onDownloadError(const QString &msg);
    void _emitProgressUpdate();
    virtual void _onVerifyProgress() override;

    virtual ssize_t _on_read(struct archive *a, const void **buff);
    virtual int _on_close(struct archive *a);
    
    // Configure libarchive options for optimal decompression performance
    void _configureArchiveOptions(struct archive *a);
    void _logCompressionFilters(struct archive *a);

    static ssize_t _archive_read(struct archive *a, void *client_data, const void **buff);
    static int _archive_close(struct archive *a, void *client_data);
};

#endif // DOWNLOADEXTRACTTHREAD_H
