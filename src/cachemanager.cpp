/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "cachemanager.h"
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <functional>

// Hash algorithm used for cache verification
#define CACHE_HASH_ALGORITHM QCryptographicHash::Sha256

CacheManager::CacheManager(QObject *parent)
    : QObject(parent)
    , workerThread_(new QThread(this))
    , worker_(new CacheVerificationWorker())
{
    // Move worker to background thread
    worker_->moveToThread(workerThread_);
    
    // Connect worker signals
    connect(worker_, &CacheVerificationWorker::verificationComplete,
            this, &CacheManager::onVerificationComplete);
    connect(worker_, &CacheVerificationWorker::diskSpaceCheckComplete,
            this, &CacheManager::onDiskSpaceCheckComplete);
    connect(worker_, &CacheVerificationWorker::verificationProgress,
            this, &CacheManager::cacheVerificationProgress);
    
    // Start worker thread
    workerThread_->start();
    
    qDebug() << "CacheManager initialized with background thread";
}

CacheManager::~CacheManager()
{
    if (workerThread_->isRunning()) {
        workerThread_->quit();
        workerThread_->wait(5000); // Wait up to 5 seconds
    }
    
    worker_->deleteLater();
}

void CacheManager::startBackgroundOperations()
{
    qDebug() << "Starting background cache operations";
    
    // Initialize cache directory and start disk space checking
    QMetaObject::invokeMethod(worker_, "checkDiskSpace", Qt::QueuedConnection);
    
    // Load cached file info from settings
    settings_.beginGroup("caching");
    QString lastFileName = settings_.value("lastFileName").toString();
    QByteArray lastHash = settings_.value("lastDownloadSHA256").toByteArray();
    settings_.endGroup();
    
    if (!lastFileName.isEmpty() && !lastHash.isEmpty()) {
        qDebug() << "Found cached file info, starting verification:" << lastFileName;
        setCacheFile(lastFileName, lastHash);
    }
}

CacheManager::CacheStatus CacheManager::getCacheStatus() const
{
    QMutexLocker locker(&mutex_);
    return status_;
}

bool CacheManager::isReady() const
{
    QMutexLocker locker(&mutex_);
    return status_.diskSpaceCheckComplete;
}

void CacheManager::setCacheFile(const QString& fileName, const QByteArray& expectedHash)
{
    updateCacheStatus([&](CacheStatus& status) {
        status.cacheFileName = fileName;
        status.cachedHash = expectedHash;
        status.isValid = false;
        status.verificationComplete = false;
    });
    
    // Start verification on background thread
    QMetaObject::invokeMethod(worker_, "verifyCacheFile", Qt::QueuedConnection,
                              Q_ARG(QString, fileName), Q_ARG(QByteArray, expectedHash));
}

void CacheManager::startCacheVerification(const QString& fileName, const QByteArray& expectedHash)
{
    qDebug() << "Starting immediate cache verification with progress for:" << fileName;
    
    updateCacheStatus([&](CacheStatus& status) {
        status.cacheFileName = fileName;
        status.cachedHash = expectedHash;
        status.isValid = false;
        status.verificationComplete = false;
    });
    
    // Start verification on background thread (same as setCacheFile, but with different logging)
    QMetaObject::invokeMethod(worker_, "verifyCacheFile", Qt::QueuedConnection,
                              Q_ARG(QString, fileName), Q_ARG(QByteArray, expectedHash));
}

void CacheManager::onVerificationComplete(bool isValid, const QString& fileName, const QByteArray& hash)
{
    qDebug() << "Cache verification complete for" << fileName << "- Valid:" << isValid;
    
    updateCacheStatus([&](CacheStatus& status) {
        status.isValid = isValid;
        status.verificationComplete = true;
        status.cacheFileName = fileName;
        status.cachedHash = hash;
    });
    
    emit cacheVerificationComplete(isValid);
    
    if (getCacheStatus().diskSpaceCheckComplete) {
        emit cacheOperationsReady();
    }
}

void CacheManager::onDiskSpaceCheckComplete(qint64 availableBytes, const QString& directory)
{
    qDebug() << "Disk space check complete:" << availableBytes / (1024*1024*1024) << "GB available in" << directory;
    
    updateCacheStatus([&](CacheStatus& status) {
        status.availableBytes = availableBytes;
        status.hasAvailableSpace = availableBytes > (1024 * 1024 * 1024); // > 1GB
        status.cacheDirectory = directory;
        status.diskSpaceCheckComplete = true;
    });
    
    emit diskSpaceCheckComplete(availableBytes);
    
    if (getCacheStatus().verificationComplete) {
        emit cacheOperationsReady();
    }
}

void CacheManager::updateCacheStatus(const std::function<void(CacheStatus&)>& updater)
{
    QMutexLocker locker(&mutex_);
    updater(status_);
}

// Worker implementation
CacheVerificationWorker::CacheVerificationWorker(QObject *parent)
    : QObject(parent)
{
}

void CacheVerificationWorker::verifyCacheFile(const QString& fileName, const QByteArray& expectedHash)
{
    qDebug() << "Background: Starting cache file verification for" << fileName;
    
    bool isValid = false;
    
    if (!expectedHash.isEmpty() && !fileName.isEmpty()) {
        QFile cacheFile(fileName);
        if (cacheFile.exists() && cacheFile.open(QIODevice::ReadOnly)) {
            // Calculate SHA256 of the actual cache file content
            QCryptographicHash hash(CACHE_HASH_ALGORITHM);
            
            qint64 fileSize = cacheFile.size();
            
            // Adaptive buffer size based on file size for optimal performance
            qint64 bufferSize;
            if (fileSize < 100 * 1024 * 1024) {        // < 100MB: use 64KB
                bufferSize = 64 * 1024;
            } else if (fileSize < 1024 * 1024 * 1024) { // 100MB-1GB: use 1MB  
                bufferSize = 1024 * 1024;
            } else if (fileSize < 4LL * 1024 * 1024 * 1024) { // 1GB-4GB: use 4MB
                bufferSize = 4 * 1024 * 1024;
            } else {                                    // > 4GB: use 8MB
                bufferSize = 8 * 1024 * 1024;
            }
            
            // Allocate buffer on heap for large sizes
            std::unique_ptr<char[]> buffer = std::make_unique<char[]>(bufferSize);
            qint64 totalBytes = 0;
            
            qDebug() << "Background: Cache verification using" << bufferSize/1024 << "KB buffer for" 
                     << fileSize/(1024*1024) << "MB file";
            
            // Emit initial progress
            emit verificationProgress(0, fileSize);
            
            while (!cacheFile.atEnd()) {
                qint64 bytesRead = cacheFile.read(buffer.get(), bufferSize);
                if (bytesRead == -1) {
                    qDebug() << "Background: Error reading cache file:" << cacheFile.errorString();
                    break;
                }
                hash.addData(QByteArrayView(buffer.get(), bytesRead));
                totalBytes += bytesRead;
                
                // Adaptive progress update frequency based on buffer size
                // Ensures responsive progress regardless of buffer size
                qint64 progressInterval = std::max(256LL * 1024, bufferSize); // At least 256KB or buffer size
                if (totalBytes % progressInterval == 0 || cacheFile.atEnd()) {
                    emit verificationProgress(totalBytes, fileSize);
                }
                
                // Allow thread interruption during long operations
                if (QThread::currentThread()->isInterruptionRequested()) {
                    qDebug() << "Background: Cache verification interrupted";
                    cacheFile.close();
                    return;
                }
            }
            
            cacheFile.close();
            
            QByteArray computedHash = hash.result().toHex();
            isValid = (computedHash == expectedHash);
            
            qDebug() << "Background: Cache verification complete";
            qDebug() << "  File size:" << totalBytes << "bytes";
            qDebug() << "  Expected hash:" << expectedHash;
            qDebug() << "  Computed hash:" << computedHash;
            qDebug() << "  Valid:" << isValid;
        } else {
            qDebug() << "Background: Cache file does not exist or cannot be opened:" << fileName;
        }
    }
    
    emit verificationComplete(isValid, fileName, expectedHash);
}

void CacheVerificationWorker::checkDiskSpace()
{
    qDebug() << "Background: Checking disk space for cache operations";
    
    // Ensure cache directory exists
    if (!ensureCacheDirectoryExists()) {
        qDebug() << "Background: Failed to create cache directory";
        emit diskSpaceCheckComplete(0, QString());
        return;
    }
    
    QString cacheDir = getCacheDirectory();
    QStorageInfo storageInfo(cacheDir);
    qint64 availableBytes = storageInfo.bytesAvailable();
    
    qDebug() << "Background: Cache directory:" << cacheDir;
    qDebug() << "Background: Available space:" << availableBytes / (1024*1024*1024) << "GB";
    
    emit diskSpaceCheckComplete(availableBytes, cacheDir);
}

bool CacheVerificationWorker::ensureCacheDirectoryExists()
{
    QString cacheDir = getCacheDirectory();
    
    if (cacheDir.isEmpty()) {
        qDebug() << "Background: Cannot determine cache directory location";
        return false;
    }
    
    QDir dir(cacheDir);
    if (!dir.exists()) {
        if (!dir.mkpath(cacheDir)) {
            qDebug() << "Background: Failed to create cache directory:" << cacheDir;
            return false;
        }
        qDebug() << "Background: Created cache directory:" << cacheDir;
    }
    
    // Test write access
    QString testFile = cacheDir + QDir::separator() + "test_write_access.tmp";
    QFile test(testFile);
    if (!test.open(QIODevice::WriteOnly)) {
        qDebug() << "Background: Cache directory exists but is not writable:" << cacheDir;
        return false;
    }
    test.write("test");
    test.close();
    QFile::remove(testFile);
    
    return true;
}

QString CacheVerificationWorker::getCacheDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}