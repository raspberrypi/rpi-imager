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
 * @brief Manages cache operations in the background to avoid blocking the UI
 * 
 * This class handles:
 * - Cache file integrity verification
 * - Disk space monitoring
 * - Cache directory setup
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
        QByteArray cachedHash;
        bool verificationComplete = false;
        bool diskSpaceCheckComplete = false;
    };

    explicit CacheManager(QObject *parent = nullptr);
    ~CacheManager();

    // Start background operations (call at app startup)
    void startBackgroundOperations();

    // Get current cache status (non-blocking)
    CacheStatus getCacheStatus() const;

    // Check if cache operations are complete
    bool isReady() const;

    // Set cache file to verify
    void setCacheFile(const QString& fileName, const QByteArray& expectedHash);
    
    // Start cache verification with progress (for immediate use)
    void startCacheVerification(const QString& fileName, const QByteArray& expectedHash);

signals:
    void cacheVerificationComplete(bool isValid);
    void diskSpaceCheckComplete(qint64 availableBytes);
    void cacheOperationsReady();
    void cacheVerificationProgress(qint64 bytesProcessed, qint64 totalBytes);

private slots:
    void onVerificationComplete(bool isValid, const QString& fileName, const QByteArray& hash);
    void onDiskSpaceCheckComplete(qint64 availableBytes, const QString& directory);

private:
    mutable QMutex mutex_;
    CacheStatus status_;
    QThread* workerThread_;
    CacheVerificationWorker* worker_;
    QSettings settings_;

    void updateCacheStatus(const std::function<void(CacheStatus&)>& updater);
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
    void verificationComplete(bool isValid, const QString& fileName, const QByteArray& hash);
    void diskSpaceCheckComplete(qint64 availableBytes, const QString& directory);
    void verificationProgress(qint64 bytesProcessed, qint64 totalBytes);

private:
    bool ensureCacheDirectoryExists();
    QString getCacheDirectory() const;
};

#endif // CACHEMANAGER_H 