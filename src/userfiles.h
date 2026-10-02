/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#ifndef USERFILES_H
#define USERFILES_H

#include <QString>
#include <QStringList>
#include <cstdio>

class QFile;

namespace PlatformQuirks {

/**
 * While in scope, this thread's file access is checked as the account that
 * invoked an elevated Imager, so a link planted in its home reaches nothing
 * more. Children don't inherit it; use runAsInvokingUser().
 */
class InvokingUserFsScope {
public:
    InvokingUserFsScope();
    ~InvokingUserFsScope();
    InvokingUserFsScope(const InvokingUserFsScope&) = delete;
    InvokingUserFsScope& operator=(const InvokingUserFsScope&) = delete;

    bool active() const { return _active; }

private:
    bool _active = false;
    [[maybe_unused]] int _savedUid = -1;
    [[maybe_unused]] int _savedGid = -1;
    [[maybe_unused]] void* _savedToken = nullptr;
};

bool invokingUser(int* uid, int* gid);

void runAsInvokingUser(QString& program, QStringList& args);

/** Opens file for writing as a new, empty file, replacing any link at its fileName(). */
bool openFreshFile(QFile& file);

/** Appends, not following a link at path. */
FILE* openLogFile(const char* path);

/**
 * Hands path, and below it if recursive, to uid:gid. Never follows a link
 * that root doesn't own, and skips hard-linked files.
 */
int reclaimOwnership(const QString& path, int uid, int gid, bool recursive);

/** Root-owned when elevated, for files a later run trusts. */
QString trustedCacheLocation();

} // namespace PlatformQuirks

#endif // USERFILES_H
