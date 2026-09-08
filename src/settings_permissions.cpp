/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "settings_permissions.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
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

int effectiveUser() { return static_cast<int>(::geteuid()); }

// -1 when the path is not there. lstat, so a symlink is reported as itself.
int ownerOf(const QString& path)
{
    struct stat st{};
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0)
        return -1;
    return static_cast<int>(st.st_uid);
}

// AT_SYMLINK_NOFOLLOW, for the same reason the mode is set on a descriptor:
// the directory this sits in belongs to an unprivileged account, and we may
// be root.
bool giveTo(const QString& path, int uid, int gid)
{
    return ::fchownat(AT_FDCWD, QFile::encodeName(path).constData(),
                      static_cast<uid_t>(uid), static_cast<gid_t>(gid),
                      AT_SYMLINK_NOFOLLOW) == 0;
}

// The account that invoked an elevated Imager, or -1. The same two variables
// applyQuirks() reads and in the same order, so the settings file ends up
// owned by whoever HOME was repointed at.
void invokingUser(int* uid, int* gid)
{
    *uid = -1;
    *gid = -1;
    if (::geteuid() != 0)
        return;

    const char* value = ::getenv("SUDO_UID");
    if (!value)
        value = ::getenv("PKEXEC_UID");
    if (!value)
        return;

    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != 0 || parsed == 0 ||
        parsed > static_cast<unsigned long>(static_cast<uid_t>(-1)))
        return;

    struct passwd pw{};
    struct passwd* found = nullptr;
    char buffer[4096];
    if (::getpwuid_r(static_cast<uid_t>(parsed), &pw, buffer, sizeof(buffer), &found) != 0
        || !found)
        return;

    *uid = static_cast<int>(found->pw_uid);
    *gid = static_cast<int>(found->pw_gid);
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

// Windows has no uid to compare against, and no elevated-run handover of the
// kind Linux has: the file belongs to whoever created it.
int effectiveUser() { return 0; }
int ownerOf(const QString& path) { return QFileInfo::exists(path) ? 0 : -1; }
bool giveTo(const QString&, int, int) { return false; }
void invokingUser(int* uid, int* gid) { *uid = -1; *gid = -1; }

#endif

} // namespace

SettingsPermissions secureSettingsFile(const QString& path, int ownerUid, int ownerGid)
{
    SettingsPermissions result;
    if (path.isEmpty())
        return result;

    const QString directory = QFileInfo(path).absolutePath();
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        const int dirOwner = ownerOf(directory);
        if (ownerUid >= 0 && dirOwner >= 0 && dirOwner != ownerUid)
            giveTo(directory, ownerUid, ownerGid);
        result.directorySecured = restrictDirectory(directory);
    }

    // QFileInfo::exists() follows symlinks; a broken one would read as absent
    // and then defeat the O_EXCL create. Ask about the link itself.
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        result.created = createRestricted(path);

    // Ownership before permissions.
    //
    // Whether we happen to own the file at this moment is the wrong
    // question. An elevated Imager creates it as root and so does own it --
    // and would then narrow it to 0600 with root as the owner, leaving the
    // person using Imager unable to open their own settings at all. What
    // matters is who *should* own it: the account that invoked us. Hand it
    // over whenever that is known and it is not already theirs. Only root
    // can, which is exactly when it is needed.
    const int owner = ownerOf(path);
    if (ownerUid >= 0 && owner >= 0 && owner != ownerUid)
        result.reowned = giveTo(path, ownerUid, ownerGid);

    const int finalOwner = result.reowned ? ownerUid : owner;
    const int us = effectiveUser();

    // Someone else's file, and not root to change that. Narrowing it anyway
    // would take the user's own settings away from them -- at 0664 they can
    // at least read what they saved. Leave it, and let the caller say so.
    if (finalOwner >= 0 && finalOwner != us && us != 0) {
        result.foreignOwner = true;
        return result;
    }

    if (!result.created)
        result.tightened = restrictExisting(path);
    else if (result.reowned)
        restrictExisting(path);   // the chown can clear setuid-ish bits; re-assert

    result.secured = isOwnerOnly(path);
    return result;
}

SettingsPermissions secureSettingsFile(const QString& path)
{
    int uid = -1;
    int gid = -1;
    invokingUser(&uid, &gid);
    return secureSettingsFile(path, uid, gid);
}

int restoreUserOwnership(const QString& path, int ownerUid, int ownerGid)
{
    if (path.isEmpty() || ownerUid < 0)
        return 0;

    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return 0;

    int changed = 0;

    // The entry itself first, so a directory we are about to walk is already
    // the user's even if the walk is cut short.
    if (ownerOf(path) != ownerUid && giveTo(path, ownerUid, ownerGid))
        ++changed;

    // isSymLink before isDir: a symlink to a directory must not be walked.
    // We changed the link itself above and that is as far as it goes.
    if (info.isSymLink() || !info.isDir())
        return changed;

    QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::System |
                          QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString entry = it.next();
        if (ownerOf(entry) != ownerUid && giveTo(entry, ownerUid, ownerGid))
            ++changed;
    }
    return changed;
}

int restoreUserOwnership(const QString& path)
{
    int uid = -1;
    int gid = -1;
    invokingUser(&uid, &gid);
    return restoreUserOwnership(path, uid, gid);
}

} // namespace rpi_imager
