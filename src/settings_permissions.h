/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#ifndef SETTINGS_PERMISSIONS_H
#define SETTINGS_PERMISSIONS_H

#include <QString>

namespace rpi_imager {

/*
 * What securing the settings file did. Reported rather than just returned as
 * a bool so a caller -- and a test -- can tell a fresh install from an
 * upgrade.
 */
struct SettingsPermissions
{
    bool created = false;            // the file was not there and was made, owner-only
    bool tightened = false;          // an existing file's permissions were narrowed
    bool secured = false;            // the file is now readable only by its owner
    bool directorySecured = false;   // and its directory likewise
    bool reowned = false;            // it belonged to another account and was handed back
    bool foreignOwner = false;       // it belongs to another account and could not be
};

/*
 * Make the QSettings file at `path` readable only by the account that owns
 * it, creating it if it is not there yet.
 */
SettingsPermissions secureSettingsFile(const QString& path, int ownerUid, int ownerGid);
SettingsPermissions secureSettingsFile(const QString& path);

/*
 * Hand a path an elevated run created in the user's home back to that user,
 * recursing into a directory.
 *
 * The settings file is not the only thing this happens to. An elevated
 * Imager writes the rpi-imager:// handler into the user's
 * ~/.local/share/applications, has update-desktop-database rewrite
 * mimeinfo.cache and xdg-mime rewrite mimeapps.list, and fills a cache tree
 * under ~/.cache -- all as root, all in a directory that belongs to somebody
 * else. On the machine this was written on, every one of those was root:root.
 */
int restoreUserOwnership(const QString& path, int ownerUid, int ownerGid);
int restoreUserOwnership(const QString& path);

} // namespace rpi_imager

#endif // SETTINGS_PERMISSIONS_H
