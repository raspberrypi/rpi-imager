/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "../userfiles.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <unistd.h>

namespace PlatformQuirks {

namespace {

bool invokingPasswd(struct passwd* pw, char* buffer, size_t size)
{
    if (::geteuid() != 0)
        return false;

    const char* value = ::getenv("SUDO_UID");
    if (!value)
        value = ::getenv("PKEXEC_UID");
    if (!value)
        return false;

    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != 0 || parsed == 0 ||
        parsed > static_cast<unsigned long>(static_cast<uid_t>(-1)))
        return false;

    struct passwd* found = nullptr;
    return ::getpwuid_r(static_cast<uid_t>(parsed), pw, buffer, size, &found) == 0 && found;
}

QStringList partsOf(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath())
        .split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

// Follows only links root owns: any other could have been planted.
int walkTo(const QStringList& parts)
{
    int fd = ::open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    for (const QString& part : parts) {
        if (fd < 0)
            break;
        const QByteArray name = QFile::encodeName(part);
        int next = ::openat(fd, name.constData(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
        struct stat st{};
        if (next >= 0 && ::fstat(next, &st) == 0 && S_ISLNK(st.st_mode)) {
            ::close(next);
            next = st.st_uid == 0 ? ::openat(fd, name.constData(), O_PATH | O_CLOEXEC) : -1;
        }
        ::close(fd);
        fd = next;
    }
    return fd;
}

int reclaimAt(int dir, const char* name, uid_t uid, gid_t gid, bool recursive, int depth)
{
    const int fd = ::openat(dir, name, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return 0;

    int changed = 0;
    struct stat st{};
    if (::fstat(fd, &st) == 0) {
        // Its other name could be anywhere, /etc/shadow included.
        const bool hardLinked = !S_ISDIR(st.st_mode) && st.st_nlink > 1;
        if (!hardLinked && st.st_uid != uid &&
            ::fchownat(fd, "", uid, gid, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW) == 0)
            ++changed;

        if (recursive && S_ISDIR(st.st_mode) && depth < 32) {
            const int listing = ::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            DIR* d = listing >= 0 ? ::fdopendir(listing) : nullptr;
            if (d) {
                while (const dirent* e = ::readdir(d)) {
                    if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0)
                        changed += reclaimAt(::dirfd(d), e->d_name, uid, gid, true, depth + 1);
                }
                ::closedir(d);
            } else if (listing >= 0) {
                ::close(listing);
            }
        }
    }
    ::close(fd);
    return changed;
}

} // namespace

InvokingUserFsScope::InvokingUserFsScope()
{
    int uid = -1;
    int gid = -1;
    if (!invokingUser(&uid, &gid))
        return;

    _savedGid = ::setfsgid(static_cast<gid_t>(gid));
    _savedUid = ::setfsuid(static_cast<uid_t>(uid));
    // Both answer with the old value whether or not they changed anything.
    if (::setfsuid(static_cast<uid_t>(-1)) != uid) {
        ::setfsuid(static_cast<uid_t>(_savedUid));
        ::setfsgid(static_cast<gid_t>(_savedGid));
        return;
    }
    _active = true;
}

InvokingUserFsScope::~InvokingUserFsScope()
{
    if (!_active)
        return;
    ::setfsuid(static_cast<uid_t>(_savedUid));
    ::setfsgid(static_cast<gid_t>(_savedGid));
}

bool invokingUser(int* uid, int* gid)
{
    *uid = -1;
    *gid = -1;
    struct passwd pw{};
    char buffer[4096];
    if (!invokingPasswd(&pw, buffer, sizeof(buffer)))
        return false;
    *uid = static_cast<int>(pw.pw_uid);
    *gid = static_cast<int>(pw.pw_gid);
    return true;
}

void runAsInvokingUser(QString& program, QStringList& args)
{
    struct passwd pw{};
    char buffer[4096];
    if (!invokingPasswd(&pw, buffer, sizeof(buffer)))
        return;
    args = QStringList{QStringLiteral("-u"), QString::fromUtf8(pw.pw_name),
                       QStringLiteral("--"), program} + args;
    program = QStringLiteral("runuser");
}

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
    InvokingUserFsScope asUser;
    constexpr int flags = O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC;
    int fd = ::open(path, flags, 0600);
    // Left root-owned by an earlier elevated run.
    if (fd < 0 && errno == EACCES && asUser.active() && ::unlink(path) == 0)
        fd = ::open(path, flags | O_EXCL, 0600);
    if (fd < 0)
        return nullptr;
    FILE* f = ::fdopen(fd, "a");
    if (!f)
        ::close(fd);
    return f;
}

int reclaimOwnership(const QString& path, int uid, int gid, bool recursive)
{
    if (path.isEmpty() || uid < 0)
        return 0;
    QStringList parts = partsOf(path);
    if (parts.isEmpty())
        return 0;
    const QByteArray leaf = QFile::encodeName(parts.takeLast());

    const int dir = walkTo(parts);
    if (dir < 0)
        return 0;
    const int changed = reclaimAt(dir, leaf.constData(), static_cast<uid_t>(uid),
                                  static_cast<gid_t>(gid), recursive, 0);
    ::close(dir);
    return changed;
}

QString trustedCacheLocation()
{
    int uid = -1;
    int gid = -1;
    if (!invokingUser(&uid, &gid))
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    // Only root can create it here, so nobody else can have planted it.
    static const char dir[] = "/var/cache/rpi-imager";
    ::mkdir(dir, 0700);
    struct stat st{};
    if (::lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != 0)
        qWarning() << "trustedCacheLocation:" << dir << "is not a root-owned directory";
    else if (st.st_mode & 077)
        ::chmod(dir, 0700);
    return QString::fromLatin1(dir);
}

} // namespace PlatformQuirks
