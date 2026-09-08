/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The repository URL a Pi can carry in its bootloader EEPROM.
 *
 * Imager running on the Pi itself reads the bootloader's configuration out
 * of nvmem at startup, and an IMAGER_REPO_URL= line there replaces the
 * default OS list. It is how a fleet points every board it images at its own
 * image server without anybody typing a URL.
 *
 * Getting it wrong is quiet in both directions. A line that should have been
 * honoured and was not sends the operator to Raspberry Pi's list without
 * saying so; a value taken when it should not have been leaves the OS list
 * empty, with the UI reporting that the data came from nowhere in
 * particular.
 *
 * Reading nvmem needs the device tree, a `find` across /sys/bus/nvmem and a
 * Pi to run on, so the parsing is separated from the fetching: this takes
 * the bytes and says what they mean.
 */

#ifndef EEPROM_REPO_OVERRIDE_H
#define EEPROM_REPO_OVERRIDE_H

#include <QByteArray>
#include <QString>

namespace rpi_eeprom {

/*
 * The repository URL the given bootloader configuration asks for, or an
 * empty string when it asks for none.
 *
 * The configuration is the raw nvmem contents: newline-separated KEY=value
 * lines, possibly with CRLF endings, possibly with trailing NULs from the
 * flash region beyond the text.
 *
 * An IMAGER_REPO_URL= with nothing after it is treated as no override rather
 * than as an override to nowhere. Both readings are defensible for a
 * malformed burn; this one leaves the operator with Raspberry Pi's list
 * instead of an empty screen, and the empty screen gives them nothing to act
 * on.
 */
QString repoUrlFromBlconfig(const QByteArray &blconfig);

} // namespace rpi_eeprom

#endif // EEPROM_REPO_OVERRIDE_H
