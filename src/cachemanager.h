#ifndef CACHEMANAGER_H
#define CACHEMANAGER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QMutex>
#include <QMutexLocker>

class CacheVerificationWorker;

/**
 * @brief Manages all cache operations in the background to avoid blocking the UI
 * 
 * This class handles:
 * - Cache file integrity verification
 * - Cache file path management
 * - Cache settings persistence
 * - Cache file invalidation
 * - Disk space monitoring
 * - Cache directory setup
 * - Custom cache file support
 * 
 * Operations are performed on background threads and results are cached
 * to avoid blocking the main UI thread during write operations.
 */
class CacheManager : public QObject
{
    Q_OBJECT

public:
    struct CacheStatus {
        bool isValid = false;
        bool hasAvailableSpace = false;
        qint64 availableBytes = 0;
        QString cacheDirectory;
        QString cacheFileName;
        QByteArray cachedHash;      // Uncompressed hash (extract_sha256) - for UI matching
        QByteArray cacheFileHash;   // Compressed hash (image_download_sha256) - for cache verification
        QByteArray computedHash;    // Computed hash from verification - for performance diagnostics
        bool verificationComplete = false;
        bool diskSpaceCheckComplete = false;
        bool customCacheFile = false;
    };

    explicit CacheManager(QObject *parent = nullptr);
    ~CacheManager();

    // Start background operations (call at app startup)
    void startBackgroundOperations();

    // Get current cache status (non-blocking)
    CacheStatus getCacheStatus() const;

    // Check if cache operations are complete
    bool isReady() const;

    // Cache file queries
    Q_INVOKABLE bool isCached(const QByteArray& expectedHash) const;
    Q_INVOKABLE bool hasPotentialCache(const QByteArray& expectedHash) const;  // Check without requiring verification
    Q_INVOKABLE QString getCacheFilePath(const QByteArray& expectedHash) const;
    
    // Cache file management
    void setCustomCacheFile(const QString& cacheFile, const QByteArray& sha256);
    void invalidateCache();
    void updateCacheFile(const QByteArray& uncompressedHash, const QByteArray& compressedHash);
    
    // Cache verification
    void startVerification(const QByteArray& expectedHash);
    
    // Cache file setup for downloads
    bool setupCacheForDownload(const QByteArray& expectedHash, qint64 downloadSize, QString& cacheFilePath);

signals:
    void cacheVerificationComplete(bool isValid);
    void diskSpaceCheckComplete(qint64 availableBytes);
    void cacheOperationsReady();
    void cacheVerificationProgress(qint64 bytesProcessed, qint64 totalBytes);
    void cacheInvalidated();
    void cacheFileUpdated(const QByteArray& uncompressedHash);

private slots:
    void onVerificationComplete(bool isValid, const QString& fileName, const QByteArray& expectedHash, const QByteArray& computedHash);
    void onDiskSpaceCheckComplete(qint64 availableBytes, const QString& directory);

private:
    mutable QMutex mutex_;
    CacheStatus status_;
    QThread* workerThread_;
    CacheVerificationWorker* worker_;
    QSettings settings_;
    bool cachingEnabled_;

    void updateCacheStatus(const std::function<void(CacheStatus&)>& updater);
    void loadCacheSettings();
    void saveCacheSettings();
    QString getDefaultCacheFilePath() const;
    bool isCachingEnabled() const;
};

/**
 * @brief Worker class that performs cache operations on a background thread
 */
class CacheVerificationWorker : public QObject
{
    Q_OBJECT

public:
    explicit CacheVerificationWorker(QObject *parent = nullptr);

public slots:
    void verifyCacheFile(const QString& fileName, const QByteArray& expectedHash);
    void checkDiskSpace();

signals:
    void verificationComplete(bool isValid, const QString& fileName, const QByteArray& expectedHash, const QByteArray& computedHash);
    void diskSpaceCheckComplete(qint64 availableBytes, const QString& directory);
    void verificationProgress(qint64 bytesProcessed, qint64 totalBytes);

private:
    bool ensureCacheDirectoryExists();
    QString getCacheDirectory() const;
};

#endif // CACHEMANAGER_H 