/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "localfileextractthread.h"
#include "config.h"
#include "systemmemorymanager.h"
#include <archive.h>
#include <archive_entry.h>

#include <QUrl>
#include <QFileInfo>
#include <QDebug>

LocalFileExtractThread::LocalFileExtractThread(const QByteArray &url, const QByteArray &dst, const QByteArray &expectedHash, QObject *parent)
    : DownloadExtractThread(url, dst, expectedHash, parent)
{
    // Prevent the machine from sleeping while the download/extraction is in progress.
    try
    {
        _suspendInhibitor = CreateSuspendInhibitor();
    }
    catch (...)
    {
        _suspendInhibitor = nullptr;
    }

    // Use the same optimal buffer sizing as compressed files for better performance
    size_t bufferSize = SystemMemoryManager::instance().getOptimalWriteBufferSize();
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

    // Release the inhibition on suspending the system.
    if (_suspendInhibitor != nullptr) {
        delete _suspendInhibitor;
    }
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

    emit preparationStatusUpdate(tr("Opening image file..."));
    _timer.start();
    _inputfile.setFileName( QUrl(_url).toLocalFile() );
    if (!_inputfile.open(_inputfile.ReadOnly))
    {
        _onDownloadError(tr("Error opening image file"));
        _closeFiles();
        return;
    }
    _lastDlTotal = _inputfile.size();
    
    emit preparationStatusUpdate(tr("Starting extraction..."));

    // Test if this file can be handled by libarchive
    bool canUseArchive = false;
    if (isImage())
    {
        canUseArchive = _testArchiveFormat();
    }
    
    if (isImage() && canUseArchive && _needsDecompressScan)
    {
        quint64 estimatedSize = _estimateDecompressedSize();
        if (estimatedSize > 0 && !_cancelled)
        {
            setExtractTotal(estimatedSize);
            qDebug() << "Estimation complete. Estimated decompressed size:" << estimatedSize;
        }
    }

    if (isImage() && canUseArchive)
        extractImageRun();  // Use libarchive for compressed/archive files
    else if (isImage() && !canUseArchive)
        extractRawImageRun();  // Direct copy for raw disk images
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

void LocalFileExtractThread::extractRawImageRun()
{
    qDebug() << "Extracting raw disk image (ISO/IMG/RAW) directly";
    
    qint64 totalBytes = _inputfile.size();
    qint64 bytesRead = 0;
    
    while (bytesRead < totalBytes && !_cancelled)
    {
        qint64 chunkSize = qMin((qint64)_inputBufSize, totalBytes - bytesRead);
        qint64 len = _inputfile.read(_inputBuf, chunkSize);
        
        if (len <= 0)
        {
            if (len < 0)
                _onDownloadError(tr("Error reading from image file"));
            break;
        }
        
        // Write the data directly to the output device
        size_t written = _writeFile(_inputBuf, len);
        if (written != (size_t)len)
        {
            _onDownloadError(tr("Error writing to device"));
            break;
        }
        
        bytesRead += len;
        _lastDlNow = bytesRead;
        
        // Emit progress updates
        _emitProgressUpdate();
    }
    
    if (!_cancelled && bytesRead == totalBytes)
    {
        qDebug() << "Raw image extraction completed successfully";
        _writeComplete();
    }
    else if (!_cancelled)
    {
        _onDownloadError(tr("Failed to read complete image file"));
    }
}

quint64 LocalFileExtractThread::_estimateDecompressedSize()
{
    emit preparationStatusUpdate(tr("Estimating compressed image size..."));

    QString filePath = QUrl(_url).toLocalFile();
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    quint64 decompressedSample = 0;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a);

    const size_t blockSize = 1024 * 1024;
    // Sample ~50MB of compressed data to get a representative compression ratio
    const qint64 compressedSampleTarget = 50 * 1024 * 1024;

    if (archive_read_open_filename(a, filePath.toLocal8Bit().constData(), blockSize) == ARCHIVE_OK)
    {
        if (archive_read_next_header(a, &entry) == ARCHIVE_OK)
        {
            // Reuse the existing heap-allocated input buffer for reading
            ssize_t size;
            while ((size = archive_read_data(a, _inputBuf, _inputBufSize)) > 0)
            {
                decompressedSample += size;
                if (_cancelled)
                    break;

                qint64 compressedConsumed = archive_filter_bytes(a, -1);
                if (compressedConsumed >= compressedSampleTarget)
                    break;
            }
        }
    }

    qint64 compressedConsumed = archive_filter_bytes(a, -1);
    archive_read_free(a);

    // Reset input file position and download counter for the actual extraction pass
    _inputfile.seek(0);
    _lastDlNow = 0;

    if (_cancelled || compressedConsumed <= 0 || decompressedSample == 0)
    {
        qDebug() << "Decompressed size estimation failed or cancelled";
        return 0;
    }

    // Calculate compression ratio and extrapolate
    QFileInfo fi(filePath);
    qint64 totalCompressedSize = fi.size();
    double ratio = static_cast<double>(decompressedSample) / static_cast<double>(compressedConsumed);
    quint64 estimated = static_cast<quint64>(totalCompressedSize * ratio);

    qDebug() << "Decompressed size estimation:"
             << "sample_compressed=" << compressedConsumed
             << "sample_decompressed=" << decompressedSample
             << "ratio=" << ratio
             << "estimated_total=" << estimated
             << QString("(%1 GB)").arg(estimated / 1073741824.0, 0, 'f', 2);

    return estimated;
}

bool LocalFileExtractThread::_testArchiveFormat()
{
    // Test if libarchive can handle this file format AND actually extract data from it
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    bool canUseArchive = false;
    
    // Configure libarchive to support all formats and filters
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a);
    
    // Save current file position
    qint64 originalPos = _inputfile.pos();
    
    // Try to open the file with libarchive
    if (archive_read_open(a, this, NULL, &LocalFileExtractThread::_archive_read_test, &LocalFileExtractThread::_archive_close_test) == ARCHIVE_OK)
    {
        // Try to read the first header
        int r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_OK)
        {
            // Header can be read, but now test if we can actually read meaningful data
            // Try to read some data from the first entry
            char testBuf[1024];
            ssize_t dataSize = archive_read_data(a, testBuf, sizeof(testBuf));
            
            if (dataSize > 0)
            {
                // libarchive can both read the header AND extract data
                canUseArchive = true;
                qDebug() << "File can be handled by libarchive as archive format";
            }
            else
            {
                // libarchive can read the header but can't extract data (likely ISO/raw disk image)
                qDebug() << "File recognized by libarchive but no extractable data found, treating as raw disk image";
            }
        }
        else
        {
            // libarchive cannot read the header
            qDebug() << "File cannot be handled by libarchive, treating as raw disk image";
        }
    }
    else
    {
        qDebug() << "Failed to open file with libarchive, treating as raw disk image";
    }
    
    // Clean up
    archive_read_free(a);
    
    // Restore original file position
    _inputfile.seek(originalPos);
    
    return canUseArchive;
}

// Static callback functions for archive format testing
ssize_t LocalFileExtractThread::_archive_read_test(struct archive *, void *client_data, const void **buff)
{
    LocalFileExtractThread *self = static_cast<LocalFileExtractThread *>(client_data);
    *buff = self->_inputBuf;
    return self->_inputfile.read(self->_inputBuf, self->_inputBufSize);
}

int LocalFileExtractThread::_archive_close_test(struct archive *, void *client_data)
{
    // Don't actually close the file during testing
    return ARCHIVE_OK;
}
