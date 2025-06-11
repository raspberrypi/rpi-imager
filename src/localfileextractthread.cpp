/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "localfileextractthread.h"
#include "config.h"
#include "buffer_optimization.h"

#include <QUrl>

LocalFileExtractThread::LocalFileExtractThread(const QByteArray &url, const QByteArray &dst, const QByteArray &expectedHash, QObject *parent)
    : DownloadExtractThread(url, dst, expectedHash, parent)
{
    // Use the same optimal buffer sizing as compressed files for better performance
    size_t bufferSize = getOptimalWriteBufferSize();
    _inputBuf = (char *) qMallocAligned(bufferSize, 4096);
    _inputBufSize = bufferSize;
}

LocalFileExtractThread::~LocalFileExtractThread()
{
    _cancelled = true;
    
    // Ensure input file is always closed to prevent file handle leaks
    if (_inputfile.isOpen()) {
        _inputfile.close();
    }
    
    wait();
    qFreeAligned(_inputBuf);
}

void LocalFileExtractThread::_cancelExtract()
{
    _cancelled = true;
    if (_inputfile.isOpen())
        _inputfile.close();
}

void LocalFileExtractThread::run()
{
    if (isImage() && !_openAndPrepareDevice())
        return;

    emit preparationStatusUpdate(tr("opening image file"));
    _timer.start();
    _inputfile.setFileName( QUrl(_url).toLocalFile() );
    if (!_inputfile.open(_inputfile.ReadOnly))
    {
        _onDownloadError(tr("Error opening image file"));
        _closeFiles();
        return;
    }
    _lastDlTotal = _inputfile.size();
    
    emit preparationStatusUpdate(tr("starting extraction"));

    if (isImage())
        extractImageRun();
    else
        extractMultiFileRun();

    if (_cancelled)
        _closeFiles();
}

ssize_t LocalFileExtractThread::_on_read(struct archive *, const void **buff)
{
    if (_cancelled)
        return -1;

    *buff = _inputBuf;
    ssize_t len = _inputfile.read(_inputBuf, _inputBufSize);

    if (len > 0)
    {
        _lastDlNow += len;
        if (!_isImage)
        {
            _inputHash.addData(_inputBuf, len);
        }
        
        // Emit progress updates for local file extraction
        _emitProgressUpdate();
    }

    return len;
}

int LocalFileExtractThread::_on_close(struct archive *)
{
    _inputfile.close();
    return 0;
}
