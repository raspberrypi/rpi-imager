/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "settings_permissions.h"
#include "userfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <fcntl.h>
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

#else

// Whether anyone beyond this account can reach the file.
//
// Qt answers from the read-only attribute unless NTFS permission lookup is
// switched on; with it on, the "other" bits are the access the Everyone SID
// has, which is the boundary that matters. The group bits are not: on
// Windows they report the owner's primary group, which is set on an ordinary
// profile directory that no second account can reach, so judging on them
// would report every installation as exposed.
//
// A path that is not there is not owner-only. Saying otherwise would report
// a file the caller failed to create as secured.
bool isOwnerOnly(const QString& path)
{
    if (!QFileInfo::exists(path))
        return false;
    QNtfsPermissionCheckGuard ntfs;
    const QFileDevice::Permissions p = QFile::permissions(path);
    return !(p & (QFileDevice::ReadOther | QFileDevice::WriteOther));
}

// Narrowing is asked for and then checked, rather than believed.
//
// QFile::setPermissions answers true here having changed nothing: an
// inherited access entry is not removed by writing a new discretionary list,
// and Imager does not write a protected one. So the attempt is made, and
// what the file ended up as is what gets reported -- a claim of success that
// left the file open to Everyone is worse than no claim at all.
bool restrictExisting(const QString& path)
{
    QNtfsPermissionCheckGuard ntfs;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return isOwnerOnly(path);
}

bool createRestricted(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::NewOnly | QIODevice::WriteOnly))
        return false;
    f.close();
    restrictExisting(path);
    return true;
}

bool restrictDirectory(const QString& path)
{
    QNtfsPermissionCheckGuard ntfs;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner);
    return isOwnerOnly(path);
}

// Windows has no uid to compare against, and no elevated-run handover of the
// kind Linux has: the file belongs to whoever created it.
int effectiveUser() { return 0; }
int ownerOf(const QString& path) { return QFileInfo::exists(path) ? 0 : -1; }

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
            PlatformQuirks::reclaimOwnership(directory, ownerUid, ownerGid, false);
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
        result.reowned = PlatformQuirks::reclaimOwnership(path, ownerUid, ownerGid, false) > 0;

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
    PlatformQuirks::invokingUser(&uid, &gid);

    // Hand back what an earlier elevated run left as root, then create and
    // narrow as the user: a link planted in their home then reaches nothing
    // they couldn't already write.
    const QString directory = QFileInfo(path).absolutePath();
    const bool reowned = !path.isEmpty() && uid >= 0 &&
        (PlatformQuirks::reclaimOwnership(directory, uid, gid, false) +
         PlatformQuirks::reclaimOwnership(path, uid, gid, false)) > 0;

    PlatformQuirks::InvokingUserFsScope asUser;
    SettingsPermissions result = asUser.active() ? secureSettingsFile(path, -1, -1)
                                                 : secureSettingsFile(path, uid, gid);
    result.reowned = result.reowned || reowned;
    return result;
}

int restoreUserOwnership(const QString& path, int ownerUid, int ownerGid)
{
    return PlatformQuirks::reclaimOwnership(path, ownerUid, ownerGid, true);
}

int restoreUserOwnership(const QString& path)
{
    int uid = -1;
    int gid = -1;
    PlatformQuirks::invokingUser(&uid, &gid);
    return restoreUserOwnership(path, uid, gid);
}

} // namespace rpi_imager
