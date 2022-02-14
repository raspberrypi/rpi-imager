/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "localfileextractthread.h"
#include "config.h"

LocalFileExtractThread::LocalFileExtractThread(const QByteArray &url, const QByteArray &dst, const QByteArray &expectedHash, QObject *parent)
    : DownloadExtractThread(url, dst, expectedHash, parent)
{
    _inputBuf = (char *) qMallocAligned(IMAGEWRITER_UNCOMPRESSED_BLOCKSIZE, 4096);
}

LocalFileExtractThread::~LocalFileExtractThread()
{
    _cancelled = true;
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
    ssize_t len = _inputfile.read(_inputBuf, IMAGEWRITER_UNCOMPRESSED_BLOCKSIZE);

    if (len > 0)
    {
        _lastDlNow += len;
        if (!_isImage)
        {
            _inputHash.addData(_inputBuf, len);
        }
    }

    return len;
}

int LocalFileExtractThread::_on_close(struct archive *)
{
    _inputfile.close();
    return 0;
}
