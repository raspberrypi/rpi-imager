/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * "Make this file unreadable", however this machine expresses that.
 *
 * A good many cases here take a file away from the user and check that the
 * code says so rather than carrying on: an image that cannot be read, a
 * destination that cannot be written, a cache directory that refuses every
 * file. All of them built the precondition with QFile::setPermissions(), and
 * on Windows that does not work -- Qt maps permissions onto the read-only
 * attribute, which has no way to say "not readable", so setPermissions()
 * returns false and the case fails in its fixture without ever reaching the
 * behaviour under test.
 *
 * Windows can express it, just not through mode bits: an explicit deny ACE
 * outranks every allow, including the owner's. icacls writes one. So rather
 * than skip a dozen cases as inapplicable, they run against the mechanism
 * Windows actually uses to deny access.
 *
 * Deny entries are removed again by restoreAccess(). A case that leaves one
 * behind leaves a file its own user cannot delete, so every helper here is
 * written to be safe to call twice and safe to call on a path that was never
 * denied.
 */
#ifndef RPI_TEST_PLATFORM_PERMISSIONS_H
#define RPI_TEST_PLATFORM_PERMISSIONS_H

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QtGlobal>

#include "platform_privilege.h"

namespace rpi_test {

#ifdef Q_OS_WIN
namespace detail {

// Everyone, as a SID literal. Named rather than looked up because the account
// database spells the group differently in every locale, and a deny ACE for
// Everyone covers the running user whatever they are called -- including the
// AzureAD\ accounts where a bare %USERNAME% does not resolve.
inline const char *kEveryoneSid() { return "*S-1-1-0"; }

inline bool icacls(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("icacls"), args);
    if (!p.waitForFinished(30000))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

} // namespace detail
#else
namespace detail {

// A directory without its execute bit cannot be searched, so nothing inside it
// can even be stat()ed -- which is not "unwritable", and on the way back
// leaves a scratch directory that cannot be emptied.
inline QFileDevice::Permissions searchBits(const QString &path)
{
    if (QFileInfo(path).isDir())
        return QFileDevice::ExeOwner | QFileDevice::ExeUser;
    return {};
}

} // namespace detail
#endif

// Take away read access. False where this machine cannot express it, which the
// caller should treat as a reason to skip rather than to fail.
inline bool denyRead(const QString &path)
{
    // Privilege beats the permission either way: root ignores mode bits, and an
    // elevated token carries SeBackupPrivilege, which reads through a deny ACE.
    // Saying no here keeps a case from "proving" a denial that never happened.
    if (isPrivileged())
        return false;

#ifdef Q_OS_WIN
    return detail::icacls({QDir::toNativeSeparators(path),
                           QStringLiteral("/deny"),
                           QString::fromLatin1(detail::kEveryoneSid()) + QStringLiteral(":(R)")});
#else
    return QFile::setPermissions(path, QFileDevice::Permissions());
#endif
}

// Take away write access, leaving the file readable.
inline bool denyWrite(const QString &path)
{
    if (isPrivileged())
        return false;

#ifdef Q_OS_WIN
    // (WD,AD) -- write data and append data -- rather than the simple (W).
    // In a deny entry (W) covers enough of the access mask that Windows
    // refuses an ordinary read as well, which is not what "unwritable" means
    // and left cases unable to check that the file they denied was untouched.
    return detail::icacls({QDir::toNativeSeparators(path),
                           QStringLiteral("/deny"),
                           QString::fromLatin1(detail::kEveryoneSid()) +
                               QStringLiteral(":(WD,AD)")});
#else
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser |
                                           detail::searchBits(path));
#endif
}

// Give it back. Safe on a path that was never denied, and safe to call twice --
// a test that fails part way through still has to leave the scratch directory
// removable.
inline void restoreAccess(const QString &path)
{
#ifdef Q_OS_WIN
    detail::icacls({QDir::toNativeSeparators(path),
                    QStringLiteral("/remove:d"),
                    QString::fromLatin1(detail::kEveryoneSid())});
#else
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                    QFileDevice::ReadUser | QFileDevice::WriteUser |
                                    detail::searchBits(path));
#endif
}

// Undoes itself, so a failing assertion cannot strand a denied file.
class DeniedAccess
{
public:
    enum Kind { Read, Write };
    DeniedAccess(const QString &path, Kind kind)
        : _path(path), _applied(kind == Read ? denyRead(path) : denyWrite(path))
    {
    }
    ~DeniedAccess()
    {
        if (_applied)
            restoreAccess(_path);
    }
    DeniedAccess(const DeniedAccess &) = delete;
    DeniedAccess &operator=(const DeniedAccess &) = delete;

    bool applied() const { return _applied; }

private:
    QString _path;
    bool _applied;
};

} // namespace rpi_test

#define REQUIRE_DENIED(denied)                                                 \
    if (!(denied).applied())                                                   \
    SKIP("this machine cannot take access away from its own user, so the "     \
         "refusal under test cannot be provoked")

#endif // RPI_TEST_PLATFORM_PERMISSIONS_H
