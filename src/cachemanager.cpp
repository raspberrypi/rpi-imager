/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "cachemanager.h"
#include "embedded_config.h"
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <QFileInfo>
#include <functional>
#include "systemmemorymanager.h"
#include "config.h"

// Hash algorithm used for cache verification (use same as OS list verification)
#define CACHE_HASH_ALGORITHM OSLIST_HASH_ALGORITHM

CacheManager::CacheManager(QObject *parent)
    : QObject(parent)
    , workerThread_(new QThread())  // Don't parent to avoid Qt's automatic deletion
    , worker_(new CacheVerificationWorker())
    , cachingEnabled_(!::isEmbeddedMode())
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
    
    // Load cache settings
    loadCacheSettings();
    
    qDebug() << "CacheManager initialized with background thread";
}

CacheManager::~CacheManager()
{
    qDebug() << "CacheManager destructor: cleaning up background thread";
    
    // Disconnect all signals to prevent any further communication
    disconnect(worker_, nullptr, this, nullptr);
    
    if (workerThread_ && workerThread_->isRunning()) {
        // Request thread to quit gracefully
        workerThread_->quit();
        
        // Wait for thread to finish
        if (!workerThread_->wait(5000)) {
            qDebug() << "CacheManager: Thread did not quit within 5 seconds, terminating";
            workerThread_->terminate();
            workerThread_->wait(2000);
        }
    }
    
    // Clean up worker object
    if (worker_) {
        delete worker_;
        worker_ = nullptr;
    }
    
    // Clean up thread object
    if (workerThread_) {
        delete workerThread_;
        workerThread_ = nullptr;
    }
    
    qDebug() << "CacheManager destructor: cleanup complete";
}

void CacheManager::startBackgroundOperations()
{
    qDebug() << "Starting background cache operations";
    
    // Initialize cache directory and start disk space checking
    QMetaObject::invokeMethod(worker_, "checkDiskSpace", Qt::QueuedConnection);
    
    // Start verification if we have cached file info
    if (!status_.cachedHash.isEmpty() && !status_.cacheFileName.isEmpty()) {
        qDebug() << "Found cached file info, starting background verification:" << status_.cacheFileName;
        startVerification(status_.cachedHash);
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

bool CacheManager::isCached(const QByteArray& expectedHash) const
{
    QMutexLocker locker(&mutex_);
    bool result = !expectedHash.isEmpty() && 
                  status_.cachedHash == expectedHash && 
                  !status_.cacheFileName.isEmpty() &&
                  QFile::exists(status_.cacheFileName) &&
                  status_.verificationComplete &&
                  status_.isValid;
    
    // Debug output removed - cache system working correctly
    
    return result;
}

bool CacheManager::hasPotentialCache(const QByteArray& expectedHash) const
{
    QMutexLocker locker(&mutex_);
    // Check if we have a potential cache match (hash matches, file exists)
    // Does NOT require verification to be complete - used to decide whether to start verification
    bool result = !expectedHash.isEmpty() && 
                  status_.cachedHash == expectedHash && 
                  !status_.cacheFileName.isEmpty() &&
                  QFile::exists(status_.cacheFileName);
    
    if (result) {
        qDebug() << "Potential cache found for hash:" << expectedHash 
                 << "file:" << status_.cacheFileName
                 << "verified:" << status_.verificationComplete
                 << "valid:" << status_.isValid;
    }
    
    return result;
}

QString CacheManager::getCacheFilePath(const QByteArray& expectedHash) const
{
    QMutexLocker locker(&mutex_);
    
    if (status_.customCacheFile) {
        // Return custom cache file path if it matches the expected hash
        return (status_.cachedHash == expectedHash) ? status_.cacheFileName : QString();
    }
    
    // Return default cache file path
    return getDefaultCacheFilePath();
}

void CacheManager::setCustomCacheFile(const QString& cacheFile, const QByteArray& sha256)
{
    qDebug() << "Setting custom cache file:" << cacheFile;
    
    updateCacheStatus([&](CacheStatus& status) {
        status.cacheFileName = cacheFile;
        status.cachedHash = QFile::exists(cacheFile) ? sha256 : QByteArray();
        status.customCacheFile = true;
        status.verificationComplete = false;
    });
    
    // Enable caching when custom cache file is set
    cachingEnabled_ = true;
}

void CacheManager::invalidateCache()
{
    qDebug() << "Invalidating cache";
    
    QString cacheFileName;
    bool customCache = false;
    
    updateCacheStatus([&](CacheStatus& status) {
        cacheFileName = status.cacheFileName;
        customCache = status.customCacheFile;
        
        // Clear cache status
        status.isValid = false;
        status.verificationComplete = false;
        status.cachedHash.clear();
        status.cacheFileHash.clear();
        if (!customCache) {
            status.cacheFileName.clear();
        }
    });
    
    // Clear settings (but not for custom cache files)
    if (!customCache) {
        settings_.beginGroup("caching");
        settings_.remove("lastDownloadSHA256");
        settings_.remove("lastCacheFileHash");
        settings_.remove("lastFileName");
        settings_.endGroup();
        settings_.sync();
    }
    
    // Try to remove the cache file
    if (!cacheFileName.isEmpty() && QFile::exists(cacheFileName)) {
        if (QFile::remove(cacheFileName)) {
            qDebug() << "Successfully removed corrupted cache file:" << cacheFileName;
        } else {
            qDebug() << "Failed to remove corrupted cache file:" << cacheFileName;
        }
    }
    
    emit cacheInvalidated();
}

void CacheManager::updateCacheFile(const QByteArray& uncompressedHash, const QByteArray& compressedHash)
{
    bool customCache = false;
    QString cacheFileName;
    
    updateCacheStatus([&](CacheStatus& status) {
        status.cachedHash = uncompressedHash;    // Store uncompressed hash for UI queries
        status.cacheFileHash = compressedHash;   // Store compressed hash for cache verification
        status.isValid = true;
        status.verificationComplete = true;
        customCache = status.customCacheFile;
        cacheFileName = status.cacheFileName;
    });
    
    // Save settings (but not for custom cache files)
    if (!customCache) {
        settings_.beginGroup("caching");
        settings_.setValue("lastDownloadSHA256", uncompressedHash);   // Store uncompressed hash for UI matching
        settings_.setValue("lastCacheFileHash", compressedHash);      // Store compressed hash for verification
        settings_.setValue("lastFileName", cacheFileName);
        settings_.endGroup();
        settings_.sync();
    }
    
    emit cacheFileUpdated(uncompressedHash); // UI matches against uncompressed hash
}

void CacheManager::startVerification(const QByteArray& expectedHash)
{
    QString cacheFileName;
    QByteArray hashToVerify;
    
    updateCacheStatus([&](CacheStatus& status) {
        if (status.customCacheFile) {
            cacheFileName = status.cacheFileName;
            hashToVerify = expectedHash; // For custom cache files, verify against expected hash
        } else {
            // Use the stored cache file path if available (loaded from settings),
            // otherwise use the default path. This ensures we verify the actual
            // cache file that exists, not a path that might differ due to app name changes.
            cacheFileName = status.cacheFileName.isEmpty() ? getDefaultCacheFilePath() : status.cacheFileName;
            // For regular cache files, verify against the stored compressed hash (cache file contains compressed data)
            hashToVerify = status.cacheFileHash.isEmpty() ? expectedHash : status.cacheFileHash;
        }
        
        status.cacheFileName = cacheFileName;
        status.cachedHash = expectedHash; // Store the expected uncompressed hash for UI matching
        status.isValid = false;
        status.verificationComplete = false;
    });
    
    // Start verification on background thread
    QMetaObject::invokeMethod(worker_, "verifyCacheFile", Qt::QueuedConnection,
                              Q_ARG(QString, cacheFileName), Q_ARG(QByteArray, hashToVerify));
}

bool CacheManager::setupCacheForDownload(const QByteArray& expectedHash, qint64 downloadSize, QString& cacheFilePath)
{
    QMutexLocker locker(&mutex_);
    
    if (!cachingEnabled_) {
        return false;
    }
    
    // Check if we have different hash than expected - need to clear old cache
    if (!status_.cachedHash.isEmpty() && status_.cachedHash != expectedHash) {
        locker.unlock();
        invalidateCache();
        locker.relock();
    }
    
    // Check disk space
    if (!status_.diskSpaceCheckComplete || !status_.hasAvailableSpace) {
        return false;
    }
    
    if (status_.availableBytes - downloadSize < IMAGEWRITER_MINIMAL_SPACE_FOR_CACHING) {
        return false;
    }
    
    // Set up cache file path
    if (status_.customCacheFile) {
        cacheFilePath = status_.cacheFileName;
    } else {
        cacheFilePath = getDefaultCacheFilePath();
        status_.cacheFileName = cacheFilePath;
    }
    
    return true;
}

void CacheManager::onVerificationComplete(bool isValid, const QString& fileName, const QByteArray& hash)
{
    QByteArray uncompressedHashForUI;
    
    updateCacheStatus([&](CacheStatus& status) {
        status.isValid = isValid;
        status.verificationComplete = true;
        status.cacheFileName = fileName;
        // Don't overwrite cachedHash - it already contains the uncompressed hash for UI matching
        // The 'hash' parameter is the compressed hash used for verification
        uncompressedHashForUI = status.cachedHash; // Get the uncompressed hash for UI update
    });
    
    qDebug() << "Cache verification:" << (isValid ? "valid" : "invalid") << fileName;
    
    emit cacheVerificationComplete(isValid);
    
    // Also emit cacheFileUpdated when verification succeeds to trigger UI update
    if (isValid && !uncompressedHashForUI.isEmpty()) {
        emit cacheFileUpdated(uncompressedHashForUI); // UI matches against uncompressed hash
    }
    
    if (getCacheStatus().diskSpaceCheckComplete) {
        emit cacheOperationsReady();
    }
}

void CacheManager::onDiskSpaceCheckComplete(qint64 availableBytes, const QString& directory)
{
    
    updateCacheStatus([&](CacheStatus& status) {
        status.availableBytes = availableBytes;
        status.hasAvailableSpace = availableBytes > IMAGEWRITER_MINIMAL_SPACE_FOR_CACHING;
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

void CacheManager::loadCacheSettings()
{
    settings_.beginGroup("caching");
    
    // Load caching enabled setting - but respect embedded mode override
    if (cachingEnabled_) {
        cachingEnabled_ = settings_.value("enabled", IMAGEWRITER_ENABLE_CACHE_DEFAULT).toBool();
    }
    
    // Load cached file info
    QString lastFileName = settings_.value("lastFileName").toString();
    QByteArray lastHash = settings_.value("lastDownloadSHA256").toByteArray();
    QByteArray cacheFileHash = settings_.value("lastCacheFileHash").toByteArray();
    
    settings_.endGroup();
    
    // Validate cache file exists and is accessible
    if (!lastFileName.isEmpty() && !lastHash.isEmpty()) {
        QFileInfo fileInfo(lastFileName);
        if (fileInfo.exists() && fileInfo.isReadable() && fileInfo.size() > 0) {
            // Test file accessibility
            QFile testFile(lastFileName);
            if (testFile.open(QIODevice::ReadOnly)) {
                testFile.close();
                
                updateCacheStatus([&](CacheStatus& status) {
                    status.cacheFileName = lastFileName;
                    status.cachedHash = lastHash;        // Uncompressed hash for UI queries
                    status.cacheFileHash = cacheFileHash; // Compressed hash for cache verification
                    status.customCacheFile = false;
                    status.verificationComplete = false;
                });
            } else {
                qDebug() << "Cache file cannot be opened, clearing settings";
                invalidateCache();
            }
        } else {
            qDebug() << "Cache file missing or unreadable, clearing settings";
            invalidateCache();
        }
    }
}

void CacheManager::saveCacheSettings()
{
    QMutexLocker locker(&mutex_);
    
    if (status_.customCacheFile) {
        return; // Don't save settings for custom cache files
    }
    
    settings_.beginGroup("caching");
    settings_.setValue("enabled", cachingEnabled_);
    settings_.setValue("lastFileName", status_.cacheFileName);
    settings_.setValue("lastDownloadSHA256", status_.cachedHash);
    settings_.setValue("lastCacheFileHash", status_.cacheFileHash);
    settings_.endGroup();
    settings_.sync();
}

QString CacheManager::getDefaultCacheFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + 
           QDir::separator() + "lastdownload.cache";
}

bool CacheManager::isCachingEnabled() const
{
    return cachingEnabled_;
}

// Worker implementation remains the same
CacheVerificationWorker::CacheVerificationWorker(QObject *parent)
    : QObject(parent)
{
}

void CacheVerificationWorker::verifyCacheFile(const QString& fileName, const QByteArray& expectedHash)
{
    bool isValid = false;
    
    if (!expectedHash.isEmpty() && !fileName.isEmpty()) {
        QFile cacheFile(fileName);
        if (cacheFile.exists() && cacheFile.open(QIODevice::ReadOnly)) {
            // Calculate SHA256 of the actual cache file content
            QCryptographicHash hash(CACHE_HASH_ALGORITHM);
            
            qint64 fileSize = cacheFile.size();
            
            // Use centralized SystemMemoryManager for consistent buffer sizing
            qint64 bufferSize = SystemMemoryManager::instance().getAdaptiveVerifyBufferSize(fileSize);
            
            // Allocate buffer on heap for large sizes
            std::unique_ptr<char[]> buffer = std::make_unique<char[]>(bufferSize);
            qint64 totalBytes = 0;
            
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
        } else {
            qDebug() << "Cache file missing or inaccessible:" << fileName;
        }
    }
    
    emit verificationComplete(isValid, fileName, expectedHash);
}

void CacheVerificationWorker::checkDiskSpace()
{
    // Ensure cache directory exists
    if (!ensureCacheDirectoryExists()) {
        qDebug() << "Failed to create cache directory";
        emit diskSpaceCheckComplete(0, QString());
        return;
    }
    
    QString cacheDir = getCacheDirectory();
    QStorageInfo storageInfo(cacheDir);
    qint64 availableBytes = storageInfo.bytesAvailable();
    
    emit diskSpaceCheckComplete(availableBytes, cacheDir);
}

bool CacheVerificationWorker::ensureCacheDirectoryExists()
{
    QString cacheDir = getCacheDirectory();
    
    if (cacheDir.isEmpty()) {
        return false;
    }
    
    QDir dir(cacheDir);
    if (!dir.exists()) {
        if (!dir.mkpath(cacheDir)) {
            qDebug() << "Failed to create cache directory:" << cacheDir;
            return false;
        }
    }
    
    // Test write access
    QString testFile = cacheDir + QDir::separator() + "test_write_access.tmp";
    QFile test(testFile);
    if (!test.open(QIODevice::WriteOnly)) {
        qDebug() << "Cache directory not writable:" << cacheDir;
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