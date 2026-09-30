// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Reading the pre-shared key out of a Windows WLAN profile.
//
// WlanGetProfile hands back the profile as XML with the key in a
// <keyMaterial> element, escaped as XML requires. What comes out of here is
// copied to the card as the network password, so a character decoded wrongly
// is a Pi that cannot join the network, with nothing on screen to say why.
//
// Kept apart from winwlancredentials.cpp so it can be tested: the rest of that
// file is WlanOpenHandle and friends, which need the service and a machine
// with a wireless card.

#ifndef RPI_IMAGER_WLAN_PROFILE_XML_H
#define RPI_IMAGER_WLAN_PROFILE_XML_H

#include <QByteArray>
#include <QRegularExpression>
#include <QString>

namespace rpi_wlan {

// XML's five predefined entities, decoded.
//
// &amp; is decoded last, and has to be: doing it first would turn the escaped
// form of "&lt;" -- which is "&amp;lt;" -- into "&lt;" and then into "<",
// handing back a character the profile never contained.
inline QString unescapeXml(QString str)
{
    static const char *table[] = {
        "&lt;", "<",
        "&gt;", ">",
        "&quot;", "\"",
        "&apos;", "'",
        "&amp;", "&"
    };
    const int tableLen = sizeof(table) / sizeof(table[0]);

    for (int i = 0; i < tableLen; i += 2) {
        str.replace(QLatin1String(table[i]), QLatin1String(table[i + 1]));
    }

    return str;
}

// The key from a profile document, or empty when it carries none.
//
// An open network has no keyMaterial at all, and a profile fetched without
// WLAN_PROFILE_GET_PLAINTEXT_KEY has it encrypted rather than absent -- so an
// empty answer here means "no key to offer", not "the key is empty".
inline QByteArray pskFromProfileXml(const QString &xml)
{
    static const QRegularExpression rx(QStringLiteral("<keyMaterial>(.+)</keyMaterial>"));
    const QRegularExpressionMatch match = rx.match(xml);
    if (!match.hasMatch())
        return {};
    return unescapeXml(match.captured(1)).toLatin1();
}

} // namespace rpi_wlan

#endif // RPI_IMAGER_WLAN_PROFILE_XML_H
