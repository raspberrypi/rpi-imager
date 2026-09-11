/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for the half of extractMultiFileRun() that only runs when the
 * operating system has *not* auto-mounted the freshly formatted card, so
 * Imager mounts the FAT partition itself. That arm ends with an unmount and
 * an rmdir of its own temporary mount point, and none of it could be reached
 * from the test binary: mounting needs CAP_SYS_ADMIN, and any device the
 * suite mounts beforehand is found by the auto-mount scan instead.
 */

#include "downloadextractthread.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QString>
#include <QTimer>

#include <cstdio>

#include <unistd.h>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <ok|truncated> <archive> <device>\n", argv[0]);
        return 2;
    }

    const QByteArray archive = QByteArray(argv[2]);
    const QByteArray device = QByteArray(argv[3]);

    std::printf("EUID=%lu\n", static_cast<unsigned long>(::geteuid()));

    DownloadExtractThread dt(QByteArray("file://") + archive, device, QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    bool finished = false;
    bool succeeded = false;
    QString errorMessage;

    QEventLoop loop;
    QObject::connect(&dt, &DownloadThread::success, &loop, [&]() {
        finished = true;
        succeeded = true;
        loop.quit();
    });
    QObject::connect(&dt, &DownloadThread::error, &loop, [&](const QString &msg) {
        finished = true;
        errorMessage = msg;
        loop.quit();
    });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(180000);

    dt.start();
    loop.exec();
    if (!finished)
        dt.cancelDownload();
    dt.wait(30000);

    std::printf("MODE=%s\n", argv[1]);
    std::printf("FINISHED=%d\n", finished ? 1 : 0);
    std::printf("SUCCEEDED=%d\n", succeeded ? 1 : 0);
    std::printf("ERROR=%s\n", errorMessage.toLocal8Bit().constData());

    std::fflush(stdout);
    return 0;
}
