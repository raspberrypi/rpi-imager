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
 *
 * Imager's settings file is not a list of preferences. It holds the crypt
 * hash of the account password that will be created on the Pi, the derived
 * WPA PSK for the wireless network the Pi is to join -- which is
 * password-equivalent, since a PSK joins the network on its own -- and, in
 * organisation mode, the Raspberry Pi Connect API key in plain text. QSettings
 * creates its file with 0666 masked by the umask, so on a typical desktop
 * that is 0644 or 0664: every other account on the machine can read all of
 * it.
 *
 * This has to run before anything writes a setting. Qt writes the file
 * through QSaveFile, which copies the permissions of the file it is
 * replacing, so a file that starts owner-only stays owner-only through every
 * later write and every later launch -- but a file that starts 0644 stays
 * 0644 for the same reason. Hence creating it ourselves rather than letting
 * the first write do it.
 *
 * An existing file is narrowed in place, which is what carries an upgrade
 * from a version that left it readable. Contents are untouched.
 *
 * The path is not followed if it is a symlink. Imager can be running as root
 * -- it elevates itself to write to a disk, and applyQuirks() then points
 * HOME back at the invoking user -- so this walks a directory that an
 * unprivileged account controls. A symlink planted there would otherwise
 * have root change the permissions of whatever it names.
 *
 * Ownership is dealt with first, because on Linux the file is very often not
 * owned by the person using Imager. An elevated run creates it as root in
 * the user's own home -- that is what applyQuirks() repointing HOME leads to
 * -- and it is left root-owned afterwards. Narrowing such a file to
 * owner-only would take the user's own settings away from them entirely: at
 * 0664 they could at least still read it.
 *
 * So when this is running elevated and the invoking user is known, the file
 * and its directory are handed back to that user before being narrowed,
 * which repairs an installation already in that state. When it is not
 * running elevated and the file belongs to somebody else, it is left exactly
 * as it is and `secured` comes back false for the caller to warn about --
 * better a readable file than one its owner cannot open.
 *
 * `ownerUid` and `ownerGid` are POSIX ids, or -1 for "leave ownership
 * alone"; they have no meaning on Windows. The single-argument form works
 * out the invoking user from the environment the elevation wrapper left.
 */
SettingsPermissions secureSettingsFile(const QString& path, int ownerUid, int ownerGid);
SettingsPermissions secureSettingsFile(const QString& path);

} // namespace rpi_imager

#endif // SETTINGS_PERMISSIONS_H
