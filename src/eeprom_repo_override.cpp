/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "eeprom_repo_override.h"

#include <QByteArrayList>

namespace rpi_eeprom {

QString repoUrlFromBlconfig(const QByteArray &blconfig)
{
    static const QByteArray key = QByteArrayLiteral("IMAGER_REPO_URL=");

    QString found;
    const QByteArrayList lines = blconfig.split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith(key))
            continue;

        // trimmed() takes the CR of a CRLF ending and any padding the flash
        // region left after the text.
        const QByteArray value = line.mid(key.size()).trimmed();
        if (value.isEmpty())
            continue;

        // The last one wins, which is what the original loop did: it kept
        // assigning without breaking. Preserved rather than changed, because
        // a configuration with two of these is already odd and nothing says
        // which the bootloader itself would honour.
        found = QString::fromUtf8(value);
    }
    return found;
}

} // namespace rpi_eeprom
