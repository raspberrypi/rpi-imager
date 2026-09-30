/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Reading the pre-shared key out of a Windows WLAN profile.
 *
 * What comes out of here is written to the card as the network password. A
 * character decoded wrongly is a Pi that silently will not join, and the user
 * has no way to see what was copied: the field is a password, so the one place
 * it could be checked shows dots.
 *
 * None of it was covered. The rest of winwlancredentials.cpp is WlanOpenHandle
 * and friends, which want the service and a wireless card, so the file sat at
 * a third and took the decoding with it.
 */

#include <catch2/catch_test_macros.hpp>

#include "windows/wlan_profile_xml.h"

#include <QString>

namespace {

// A profile document shaped like the one WlanGetProfile returns.
QString profileWithKey(const QString &escapedKey)
{
    return QStringLiteral(
               "<?xml version=\"1.0\"?>\n"
               "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
               "  <name>Home</name>\n"
               "  <MSM><security><sharedKey>\n"
               "    <keyType>passPhrase</keyType>\n"
               "    <protected>false</protected>\n"
               "    <keyMaterial>%1</keyMaterial>\n"
               "  </sharedKey></security></MSM>\n"
               "</WLANProfile>\n")
        .arg(escapedKey);
}

} // namespace

TEST_CASE("An ordinary passphrase comes back as it was stored", "[wlan]")
{
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(QStringLiteral("correcthorse")))
          == QByteArray("correcthorse"));
}

TEST_CASE("A profile with no key at all yields nothing", "[wlan]")
{
    // An open network, or a profile fetched without the plaintext-key flag.
    const QString open = QStringLiteral(
        "<WLANProfile><name>Cafe</name>"
        "<MSM><security><authEncryption><authentication>open</authentication>"
        "</authEncryption></security></MSM></WLANProfile>");
    CHECK(rpi_wlan::pskFromProfileXml(open).isEmpty());
}

TEST_CASE("The five XML entities are decoded", "[wlan]")
{
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(QStringLiteral("a&amp;b")))
          == QByteArray("a&b"));
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(QStringLiteral("a&lt;b")))
          == QByteArray("a<b"));
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(QStringLiteral("a&gt;b")))
          == QByteArray("a>b"));
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(QStringLiteral("a&quot;b")))
          == QByteArray("a\"b"));
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(QStringLiteral("a&apos;b")))
          == QByteArray("a'b"));
}

TEST_CASE("An escaped ampersand is not decoded twice", "[wlan]")
{
    // The order the table is walked in is what decides this. Decoding &amp;
    // before &lt; would take "&amp;lt;" -- which is how a passphrase
    // containing the literal text "&lt;" is stored -- to "<", a character the
    // user never typed. &amp; is decoded last for exactly this reason.
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(QStringLiteral("&amp;lt;")))
          == QByteArray("&lt;"));
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(QStringLiteral("&amp;amp;")))
          == QByteArray("&amp;"));
}

TEST_CASE("A passphrase of punctuation survives intact", "[wlan]")
{
    // 802.11i allows any printable ASCII, and a password manager will happily
    // produce a passphrase made mostly of it.
    const QString escaped = QStringLiteral("p&amp;ss&lt;w&gt;rd!#$%^*()_+-=[]{}|;:,./?");
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(escaped))
          == QByteArray("p&ss<w>rd!#$%^*()_+-=[]{}|;:,./?"));
}

TEST_CASE("A 64-character hex key is carried whole", "[wlan]")
{
    // The other thing the standard allows in place of a passphrase, and the
    // one where a single dropped character is hardest to spot.
    const QString hex = QString(64, QLatin1Char('a'));
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(hex)) == hex.toLatin1());
}

TEST_CASE("The shortest and longest passphrases the standard allows", "[wlan]")
{
    const QString shortest = QStringLiteral("12345678");
    const QString longest = QString(63, QLatin1Char('x'));
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(shortest)) == shortest.toLatin1());
    CHECK(rpi_wlan::pskFromProfileXml(profileWithKey(longest)) == longest.toLatin1());
}

TEST_CASE("A document that is not a profile yields nothing", "[wlan]")
{
    CHECK(rpi_wlan::pskFromProfileXml(QString()).isEmpty());
    CHECK(rpi_wlan::pskFromProfileXml(QStringLiteral("not xml at all")).isEmpty());
    // An opening tag with no closing one is not a key.
    CHECK(rpi_wlan::pskFromProfileXml(QStringLiteral("<keyMaterial>abc")).isEmpty());
}

TEST_CASE("Unescaping leaves an unrecognised entity alone", "[wlan]")
{
    // &nbsp; is not one of XML's five, and guessing at it would change the
    // passphrase. Carried through as written.
    CHECK(rpi_wlan::unescapeXml(QStringLiteral("a&nbsp;b")) == QStringLiteral("a&nbsp;b"));
}
