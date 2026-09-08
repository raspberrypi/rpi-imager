/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "settings_permissions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rpi_imager {

namespace {

#ifdef Q_OS_UNIX

// 0600 on a descriptor we opened ourselves with O_NOFOLLOW, so a symlink at
// the path cannot redirect the change onto something else.
bool restrictExisting(const QString& path)
{
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool ok = ::fchmod(fd, S_IRUSR | S_IWUSR) == 0;
    ::close(fd);
    return ok;
}

// O_EXCL refuses to create through an existing symlink, and the mode is
// applied by the kernel at creation -- there is no window in which the file
// exists with wider permissions.
bool createRestricted(const QString& path)
{
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                          S_IRUSR | S_IWUSR);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

bool restrictDirectory(const QString& path)
{
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool ok = ::fchmod(fd, S_IRUSR | S_IWUSR | S_IXUSR) == 0;
    ::close(fd);
    return ok;
}

bool isOwnerOnly(const QString& path)
{
    struct stat st{};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return false;
    return (st.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

#else

// Windows has no umask and no mode bits; Qt maps the owner permissions onto
// the file's ACL where NTFS permission lookup is enabled, and onto the
// read-only attribute where it is not. Best effort, and the same shape.
bool restrictExisting(const QString& path)
{
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool createRestricted(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::NewOnly | QIODevice::WriteOnly))
        return false;
    f.close();
    return restrictExisting(path);
}

bool restrictDirectory(const QString& path)
{
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ExeOwner);
}

bool isOwnerOnly(const QString& path)
{
    const QFileDevice::Permissions p = QFile::permissions(path);
    return !(p & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                  QFileDevice::ReadOther | QFileDevice::WriteOther));
}

#endif

} // namespace

SettingsPermissions secureSettingsFile(const QString& path)
{
    SettingsPermissions result;
    if (path.isEmpty())
        return result;

    const QString directory = QFileInfo(path).absolutePath();
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        result.directorySecured = restrictDirectory(directory);
    }

    // QFileInfo::exists() follows symlinks; a broken one would read as absent
    // and then defeat the O_EXCL create. Ask about the link itself.
    const QFileInfo info(path);
    if (info.exists() || info.isSymLink()) {
        result.tightened = restrictExisting(path);
    } else {
        result.created = createRestricted(path);
    }

    result.secured = isOwnerOnly(path);
    return result;
}

} // namespace rpi_imager
