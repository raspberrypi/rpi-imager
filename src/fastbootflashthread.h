/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * QThread that flashes an OS image to a device via the fastboot protocol.
 *
 * Uses a three-thread pipeline:
 *   Thread 1 (download):   curl → _compressedRing
 *   Thread 2 (decompress): _compressedRing → libarchive → _decompressedRing
 *   Main thread (flash):   _decompressedRing → fastboot download+flash → USB
 */

#ifndef FASTBOOTFLASHTHREAD_H
#define FASTBOOTFLASHTHREAD_H

#include <QThread>
#include <QString>
#include <QUrl>

#include <memory>

class RingBuffer;
class AcceleratedCryptographicHash;

class FastbootFlashThread : public QThread
{
    Q_OBJECT
public:
    explicit FastbootFlashThread(const QString& fastbootId,
                                  const QUrl& imageUrl,
                                  quint64 downloadLen,
                                  quint64 extractLen,
                                  const QByteArray& expectedHash,
                                  QObject* parent = nullptr);
    ~FastbootFlashThread() override;

    void cancel();

signals:
    void success();
    void error(QString msg);
    void finalizing();
    void preparationStatusUpdate(QString msg);
    void downloadProgress(quint64 dlnow, quint64 dltotal);
    void writeProgress(quint64 now, quint64 total);
    void eventFastbootDeviceOpen(quint32 durationMs, bool success, QString metadata);

protected:
    void run() override;

private:
    void downloadProducer();
    void decompressConsumerProducer();

    QString _fastbootId;
    QUrl _imageUrl;
    quint64 _downloadLen;
    quint64 _extractLen;
    QByteArray _expectedHash;
    std::atomic<bool> _cancelled{false};

    // Pipeline ring buffers
    std::unique_ptr<RingBuffer> _compressedRing;
    std::unique_ptr<RingBuffer> _decompressedRing;

    // Incremental hash for verification
    std::unique_ptr<AcceleratedCryptographicHash> _imageHash;

    // Track download progress from curl callback
    std::atomic<quint64> _dlnow{0};
    std::atomic<quint64> _dltotal{0};

    // Error from pipeline threads
    QString _downloadError;
    QString _decompressError;
};

#endif // FASTBOOTFLASHTHREAD_H
