#ifndef LOCALFILEEXTRACTTHREAD_H
#define LOCALFILEEXTRACTTHREAD_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadextractthread.h"
#include "suspend_inhibitor.h"
#include <QFile>

// Forward declarations for libarchive
struct archive;
struct archive_entry;

class LocalFileExtractThread : public DownloadExtractThread
{
    Q_OBJECT
public:
    explicit LocalFileExtractThread(const QByteArray &url, const QByteArray &dst = "", const QByteArray &expectedHash = "", QObject *parent = nullptr);
    virtual ~LocalFileExtractThread();
    void setNeedsDecompressScan(bool needs) { _needsDecompressScan = needs; }

protected:
    virtual void _cancelExtract();
    virtual void run();
    virtual ssize_t _on_read(struct archive *a, const void **buff);
    virtual int _on_close(struct archive *a);
    void extractRawImageRun();
    quint64 _estimateDecompressedSize();
    bool _testArchiveFormat();
    static ssize_t _archive_read_test(struct archive *, void *client_data, const void **buff);
    static int _archive_close_test(struct archive *, void *client_data);
    QFile _inputfile;
    char *_inputBuf;
    size_t _inputBufSize;

private:
    SuspendInhibitor *_suspendInhibitor;
    bool _needsDecompressScan = false;
};

#endif // LOCALFILEEXTRACTTHREAD_H
