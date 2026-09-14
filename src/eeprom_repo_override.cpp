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

        // Where the text ends.
        //
        // A region is a fixed size and is padded to it -- with nought if it
        // was zeroed, with 0xFF if it was erased -- and there is no reason
        // for the writer to leave a newline between the value and that
        // padding. Neither byte is whitespace, so trimmed() keeps both, and
        // the URL came back with the rest of the region on the end of it.
        // Neither can occur in valid UTF-8 either, so the first of them is
        // the end of the text whatever follows.
        QByteArray value = line.mid(key.size());
        for (qsizetype i = 0; i < value.size(); ++i) {
            const uchar b = static_cast<uchar>(value.at(i));
            if (b == 0x00 || b == 0xFF) {
                value.truncate(i);
                break;
            }
        }

        // trimmed() takes the CR of a CRLF ending and any spaces around it.
        value = value.trimmed();
        if (value.isEmpty())
            continue;

        // Trimmed again after decoding, and not only for tidiness: a byte
        // order mark is not whitespace, so the trim above steps over it and
        // stops -- and then fromUtf8() removes the mark, uncovering whatever
        // was behind it. A value written as BOM + vertical tab + text came
        // back with the tab still on the front. Found by fuzzing.
        //
        // The second emptiness check is the same case with nothing after the
        // whitespace: a mark and a tab and no text at all is not a value.
        const QString decoded = QString::fromUtf8(value).trimmed();
        if (decoded.isEmpty())
            continue;

        // The last one wins, which is what the original loop did: it kept
        // assigning without breaking. Preserved rather than changed, because
        // a configuration with two of these is already odd and nothing says
        // which the bootloader itself would honour.
        found = decoded;
    }
    return found;
}

} // namespace rpi_eeprom
