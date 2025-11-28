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

protected:
    char *_abuf[2];
    size_t _abufsize;
    _extractThreadClass *_extractThread;
    
    // Zero-copy ring buffer for producer-consumer data transfer
    std::unique_ptr<RingBuffer> _ringBuffer;
    static const int RING_BUFFER_SLOTS;  // Number of slots in ring buffer
    RingBuffer::Slot* _currentReadSlot;  // Current slot being read by libarchive
    
    bool _ethreadStarted, _isImage;
    AcceleratedCryptographicHash _inputHash;
    int _activeBuf;
    bool _writeThreadStarted;
    QFuture<size_t> _writeFuture;
    bool _progressStarted;
    qint64 _lastProgressTime;
    quint64 _lastEmittedDlNow, _lastLocalVerifyNow;
    quint64 _lastEmittedDecompressNow, _lastEmittedWriteNow;
    std::atomic<quint64> _bytesDecompressed;  // Total bytes output from decompressor
    bool _downloadComplete;

    void _pushQueue(const char *data, size_t len);
    void _cancelExtract();
    virtual size_t _writeData(const char *buf, size_t len);
    virtual void _onDownloadSuccess();
    virtual void _onDownloadError(const QString &msg);
    void _emitProgressUpdate();
    virtual bool _verify();

    virtual ssize_t _on_read(struct archive *a, const void **buff);
    virtual int _on_close(struct archive *a);
    
    // Configure libarchive options for optimal decompression performance
    void _configureArchiveOptions(struct archive *a);
    void _logCompressionFilters(struct archive *a);

    static ssize_t _archive_read(struct archive *a, void *client_data, const void **buff);
    static int _archive_close(struct archive *a, void *client_data);
};

#endif // DOWNLOADEXTRACTTHREAD_H
