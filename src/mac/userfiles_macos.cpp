/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "../userfiles.h"

#include <QDebug>
#include <QFile>
#include <QStandardPaths>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Imager never runs elevated on macOS: authopen hands it a disk's descriptor
// instead. There is no other account's home to guard, only links at a path.

namespace PlatformQuirks {

InvokingUserFsScope::InvokingUserFsScope() = default;
InvokingUserFsScope::~InvokingUserFsScope() = default;

bool invokingUser(int* uid, int* gid)
{
    *uid = -1;
    *gid = -1;
    return false;
}

void runAsInvokingUser(QString&, QStringList&) {}

bool openFreshFile(QFile& file)
{
    const QByteArray path = QFile::encodeName(file.fileName());
    struct stat st{};
    int fd = -1;
    if (::lstat(path.constData(), &st) == 0 && S_ISCHR(st.st_mode)) {
        // Such as /dev/full. Only root makes devices, so none was planted.
        fd = ::open(path.constData(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
    } else {
        if (::unlink(path.constData()) != 0 && errno != ENOENT) {
            qDebug() << "openFreshFile: Failed to replace" << file.fileName() << "-" << std::strerror(errno);
            return false;
        }
        fd = ::open(path.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    }
    if (fd < 0) {
        qDebug() << "openFreshFile: Failed to create" << file.fileName() << "-" << std::strerror(errno);
        return false;
    }
    if (!file.open(fd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
    return true;
}

FILE* openLogFile(const char* path)
{
    const int fd = ::open(path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return nullptr;
    FILE* f = ::fdopen(fd, "a");
    if (!f)
        ::close(fd);
    return f;
}

int reclaimOwnership(const QString&, int, int, bool) { return 0; }

QString trustedCacheLocation()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

} // namespace PlatformQuirks
