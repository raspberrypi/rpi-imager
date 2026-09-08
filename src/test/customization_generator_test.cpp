/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "customization_generator.h"
#include "dependencies/sha256crypt/sha256crypt.h"
#include "dependencies/yescrypt/yescrypt_wrapper.h"
#include <QVariantMap>
#include <QString>
#include <QByteArray>
#include <QPasswordDigestor>
#include <QCryptographicHash>
#include <QStringConverter>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "fixture_process.h"

using namespace rpi_imager;
using Catch::Matchers::ContainsSubstring;

// ---------------------------------------------------------------------------
// Quoting what the user typed, for a script that runs as root
// ---------------------------------------------------------------------------
//
// Every value the user supplies -- username, password, network name,
// passphrase, country, keymap, timezone, SSH keys -- is pasted into
// firstrun.sh as a command argument, and firstrun.sh runs as root on the
// first boot. shellQuote() is the only thing between a password containing a
// semicolon and a command running on somebody's Pi. It had no test of its own.
//
// Checked by running the shell rather than by reading the string. "It
// contains the right characters" is a weaker claim than "sh reads it back as
// exactly what went in", and the second is the one that matters.

namespace {

// What /bin/sh makes of a quoted value: printf writes it back verbatim, so
// anything the shell interpreted shows up as a difference.
QString throughTheShell(const QString &value, bool *ran)
{
    QProcess sh;
    sh.start(QStringLiteral("/bin/sh"),
             {QStringLiteral("-c"),
              QStringLiteral("printf %s ") + CustomisationGenerator::shellQuote(value)});
    *ran = sh.waitForFinished(10000) && sh.exitStatus() == QProcess::NormalExit;
    return QString::fromUtf8(sh.readAllStandardOutput());
}

} // namespace

TEST_CASE("A quoted value reaches the shell exactly as it was typed",
          "[customization][shellquoting]") {
    struct Case { const char *what; const char *value; };
    const Case cases[] = {
        {"an ordinary hostname",          "raspberrypi"},
        {"a password with a space",       "correct horse battery"},
        {"an apostrophe",                 "o'brien"},
        {"nothing but apostrophes",       "\'\'\'"},
        {"a semicolon and a command",     "x'; touch /tmp/rpi-imager-pwned; '"},
        {"command substitution",          "$(id)"},
        {"backticks",                     "`id`"},
        {"a variable",                    "$HOME and ${PATH}"},
        {"a pipe and a redirect",         "a | b > c < d"},
        {"an ampersand",                  "a && b & c"},
        {"a newline",                     "first\nsecond"},
        {"a backslash",                   "back\\slash"},
        {"double quotes",                 "say \"hello\""},
        {"a glob",                        "*.img ?x [a-z]"},
        {"a network name with all of it", "My Wi-Fi's $network `here`"},
    };

    for (const Case &c : cases) {
        const QString value = QString::fromUtf8(c.value);
        bool ran = false;
        const QString seen = throughTheShell(value, &ran);
        INFO(c.what << ": quoted as "
             << CustomisationGenerator::shellQuote(value).toStdString());
        REQUIRE(ran);
        CHECK(seen == value);
    }

    // Nothing above was allowed to actually run.
    CHECK_FALSE(QFile::exists(QStringLiteral("/tmp/rpi-imager-pwned")));
}

TEST_CASE("A quoted value is a single shell word", "[customization][shellquoting]") {
    // Splitting matters as much as interpreting: a passphrase with a space
    // that arrives as two arguments configures the wrong network and drops
    // the rest on the floor, with nothing to say so.
    QProcess sh;
    sh.start(QStringLiteral("/bin/sh"),
             {QStringLiteral("-c"),
              QStringLiteral("set -- ")
                  + CustomisationGenerator::shellQuote(
                        QStringLiteral("two words $and 'more'"))
                  + QStringLiteral("; printf %s $#")});
    REQUIRE(sh.waitForFinished(10000));
    CHECK(QString::fromUtf8(sh.readAllStandardOutput()) == QStringLiteral("1"));
}

TEST_CASE("CustomisationGenerator generates valid sh script header", "[customization]") {
    QVariantMap settings;
    settings["hostname"] = "testpi";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("#!/bin/sh"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set +e"));
}

TEST_CASE("CustomisationGenerator handles hostname configuration", "[customization]") {
    QVariantMap settings;
    settings["hostname"] = "testpi";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("CURRENT_HOSTNAME=$(cat /etc/hostname"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("raspberrypi-sys-mods/imager_custom"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_hostname testpi"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("echo testpi >/etc/hostname"));
}

TEST_CASE("CustomisationGenerator sets FIRSTUSER variables early", "[customization]") {
    QVariantMap settings;
    settings["hostname"] = "testpi";
    settings["sshUserName"] = "user1";
    settings["sshAuthorizedKeys"] = "ssh-rsa AAAAB3...testkey";  // Add SSH key to trigger SSH setup
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Check that FIRSTUSER is defined after hostname but before most other operations
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("FIRSTUSER=$(getent passwd 1000"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("FIRSTUSERHOME=$(getent passwd 1000"));
    
    // Verify FIRSTUSER appears before any SSH key operations
    int firstUserPos = scriptStr.indexOf("FIRSTUSER=$(getent");
    int sshKeyPos = scriptStr.indexOf(".ssh/authorized_keys");
    REQUIRE(firstUserPos < sshKeyPos);
}

TEST_CASE("CustomisationGenerator handles SSH keys with heredoc", "[customization]") {
    QVariantMap settings;
    settings["sshEnabled"] = true;
    settings["sshAuthorizedKeys"] = "ssh-rsa AAAAB3...key1\nssh-rsa AAAAB3...key2";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Check for heredoc usage (not process substitution)
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("cat > \"$FIRSTUSERHOME/.ssh/authorized_keys\" <<'EOF'"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssh-rsa AAAAB3...key1"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssh-rsa AAAAB3...key2"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("EOF"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("PasswordAuthentication no"));
}

TEST_CASE("CustomisationGenerator handles user renaming with userconf", "[customization]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$5$fakesalt$fakehash123";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Check for userconf integration
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("/usr/lib/userconf-pi/userconf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("'testuser'"));
    
    // Check for fallback user renaming logic
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("usermod -l"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("usermod -m -d"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("groupmod -n"));
    
    // Check for lightdm, getty, and sudoers updates
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("lightdm/lightdm.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("getty@tty1.service.d/autologin.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("010_pi-nopasswd"));
}

TEST_CASE("CustomisationGenerator handles yescrypt password format", "[customization][password]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$y$j9T$saltsaltsalt$hashhashhashhashhashhashhashhashhashhash";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Check that yescrypt password is properly passed through
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("/usr/lib/userconf-pi/userconf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("$y$j9T$"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("echo \"$FIRSTUSER:$y$j9T$"));
}

// Regression test for issue #1627. A password pasted from a browser or password
// manager arrives with a trailing newline, because Qt's single-line text fields
// insert clipboard content verbatim. PAM discards the line terminator when
// reading a password, so hashing the raw value yields a hash that can never be
// matched at login. cryptPassword() must therefore strip CR/LF before hashing.
//
// Verified the way PAM would: re-derive the hash from the *clean* password using
// the stored hash as the salt setting, and require that it reproduces the hash
// that was generated from the newline-bearing input.
TEST_CASE("cryptPassword strips CR/LF so pasted passwords still authenticate",
          "[customization][password]") {
    const QByteArray clean = "correct horse battery staple";

    SECTION("sha256crypt (pre-2023 OS)") {
        const QString releaseDate = QStringLiteral("2022-09-22");
        REQUIRE_FALSE(CustomisationGenerator::osUsesYescrypt(releaseDate));

        for (const QByteArray &suffix : {QByteArray("\n"), QByteArray("\r\n"), QByteArray("\r")}) {
            const QString hash = CustomisationGenerator::cryptPassword(clean + suffix, releaseDate);
            REQUIRE(hash.startsWith(QStringLiteral("$5$")));
            const QByteArray setting = hash.toUtf8();
            REQUIRE(QString::fromUtf8(sha256_crypt(clean.constData(), setting.constData())) == hash);
        }
    }

    SECTION("yescrypt (2023+ OS)") {
        const QString releaseDate = QStringLiteral("2024-03-15");
        REQUIRE(CustomisationGenerator::osUsesYescrypt(releaseDate));

        for (const QByteArray &suffix : {QByteArray("\n"), QByteArray("\r\n"), QByteArray("\r")}) {
            const QString hash = CustomisationGenerator::cryptPassword(clean + suffix, releaseDate);
            REQUIRE(CustomisationGenerator::isYescryptHash(hash));
            const QByteArray setting = hash.toUtf8();
            REQUIRE(QString::fromUtf8(yescrypt_crypt(clean.constData(), setting.constData())) == hash);
        }
    }

    SECTION("a genuinely different password still does not authenticate") {
        const QString releaseDate = QStringLiteral("2024-03-15");
        const QString hash = CustomisationGenerator::cryptPassword(clean + "\n", releaseDate);
        const QByteArray setting = hash.toUtf8();
        REQUIRE(QString::fromUtf8(yescrypt_crypt("wrong password", setting.constData())) != hash);
    }

    SECTION("interior CR/LF is removed too, not just a trailing terminator") {
        // A multi-line clipboard paste collapses to a single line rather than
        // being silently truncated at the first newline.
        const QString releaseDate = QStringLiteral("2024-03-15");
        const QString hash = CustomisationGenerator::cryptPassword("ab\ncd", releaseDate);
        const QByteArray setting = hash.toUtf8();
        REQUIRE(QString::fromUtf8(yescrypt_crypt("abcd", setting.constData())) == hash);
        REQUIRE(QString::fromUtf8(yescrypt_crypt("ab", setting.constData())) != hash);
    }
}

// Companion to the test above, for the Wi-Fi passphrase rather than the account
// password. Here a stray newline does more than corrupt the derivation: the
// 8..63 passphrase-length test decides whether the value is treated as a
// passphrase to hash or as an already-computed 64-hex PMK to pass through, so a
// single extra character can flip the branch and emit the user's plaintext where
// a PMK is expected.
TEST_CASE("resolveWifiPskCrypt strips CR/LF before classifying by length",
          "[customization][wifi][password]") {
    const QByteArray ssid = "TestNet";

    // resolveWifiPskCrypt is private, so drive it through generateSystemdScript
    // and read back the PSK it emits into the wpa_supplicant stanza.
    auto pskFor = [&](const QString &plaintext) {
        QVariantMap settings;
        settings["wifiConfigured"] = true;
        settings["wifiSSID"] = QString::fromUtf8(ssid);
        settings["wifiPassword"] = plaintext;
        const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));
        static const QRegularExpression pskRe(QStringLiteral("(?m)^\\s*psk=(\\S*)\\s*$"));
        const QRegularExpressionMatch m = pskRe.match(script);
        REQUIRE(m.hasMatch());
        return m.captured(1);
    };

    SECTION("a trailing newline does not change the derived PSK") {
        const QString expected = pskFor(QStringLiteral("hunter2hunter2"));
        REQUIRE_FALSE(expected.isEmpty());
        REQUIRE(pskFor(QStringLiteral("hunter2hunter2\n")) == expected);
        REQUIRE(pskFor(QStringLiteral("hunter2hunter2\r\n")) == expected);
    }

    SECTION("a 63-character passphrase is still hashed, not passed through") {
        const QString maxLen(63, QLatin1Char('a'));
        const QString expected = pskFor(maxLen);
        // A derived PSK is 32 bytes rendered as hex; the plaintext must not survive.
        REQUIRE(expected.length() == 64);
        REQUIRE(expected != maxLen);
        // Without stripping, 63 + 1 == 64 would take the pass-through branch.
        REQUIRE(pskFor(maxLen + "\n") == expected);
    }

    SECTION("a too-short passphrase is not inflated into a valid length") {
        const QString tooShort(7, QLatin1Char('a'));
        // 7 chars is below the WPA minimum, so it is passed through unchanged
        // rather than hashed. Adding a newline must not make it look like 8.
        REQUIRE(pskFor(tooShort) == tooShort);
        REQUIRE(pskFor(tooShort + "\n") == tooShort);
    }

    SECTION("a real 64-hex PMK is still passed through untouched") {
        const QString pmk(64, QLatin1Char('a'));
        REQUIRE(pskFor(pmk) == pmk);
        REQUIRE(pskFor(pmk + "\n") == pmk);
    }
}

TEST_CASE("CustomisationGenerator handles sha256crypt password format", "[customization][password]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$5$rounds=5000$saltsalt$hashhashhashhashhashhash";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Check that sha256crypt password is properly passed through
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("/usr/lib/userconf-pi/userconf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("$5$rounds=5000$"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("echo \"$FIRSTUSER:$5$rounds=5000$"));
}

TEST_CASE("CustomisationGenerator handles timezone and keyboard at end", "[customization]") {
    QVariantMap settings;
    settings["timezone"] = "Europe/London";
    settings["keyboard"] = "gb";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Check timezone configuration
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_timezone 'Europe/London'"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("echo \"Europe/London\" >/etc/timezone"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("dpkg-reconfigure -f noninteractive tzdata"));
    
    // Check keyboard configuration
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_keymap 'gb'"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("cat >/etc/default/keyboard <<'KBEOF'"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("XKBLAYOUT=\"gb\""));
    
    // Verify timezone/keyboard come after user operations but before cleanup
    int userPos = scriptStr.indexOf("userconf-pi/userconf");
    int timezonePos = scriptStr.indexOf("set_timezone");
    int cleanupPos = scriptStr.indexOf("rm -f /boot/firstrun.sh");
    
    REQUIRE(userPos < timezonePos);
    REQUIRE(timezonePos < cleanupPos);
}

TEST_CASE("CustomisationGenerator includes cleanup at end", "[customization]") {
    QVariantMap settings;
    settings["hostname"] = "test";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("rm -f /boot/firstrun.sh"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("sed -i 's| systemd.run.*||g' /boot/cmdline.txt"));
    REQUIRE(scriptStr.endsWith("exit 0\n"));
}

TEST_CASE("CustomisationGenerator reference script comparison", "[customization][reference]") {
    // Reference configuration that matches the working old imager script
    QVariantMap settings;
    settings["hostname"] = "raspberrypi";
    settings["sshEnabled"] = true;
    settings["sshUserName"] = "testuserfoobar";
    settings["sshUserPassword"] = "$5$salt$hash";
    settings["sshAuthorizedKeys"] = "ssh-rsa AAAAB3...testkey";
    settings["timezone"] = "Europe/London";
    settings["keyboard"] = "gb";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Key structural checks from the reference script
    SECTION("Script structure matches reference") {
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("#!/bin/sh"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set +e"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("CURRENT_HOSTNAME=$(cat /etc/hostname | tr -d"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("FIRSTUSER=$(getent passwd 1000 | cut -d: -f1)"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("FIRSTUSERHOME=$(getent passwd 1000 | cut -d: -f6)"));
    }
    
    SECTION("SSH key handling matches reference") {
        // Should use heredoc, not process substitution
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("cat > \"$FIRSTUSERHOME/.ssh/authorized_keys\" <<'EOF'"));
        REQUIRE_FALSE(scriptStr.contains("install -o \"$FIRSTUSER\" -m 600 <(printf"));
    }
    
    SECTION("User management matches reference") {
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("/usr/lib/userconf-pi/userconf"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("echo \"$FIRSTUSER:$5$salt$hash"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("if [ \"$FIRSTUSER\" != \"testuserfoobar\" ]; then"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("usermod -l \"testuserfoobar\" \"$FIRSTUSER\""));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("usermod -m -d \"/home/testuserfoobar\" \"testuserfoobar\""));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("groupmod -n \"testuserfoobar\" \"$FIRSTUSER\""));
    }
    
    SECTION("Locale configuration matches reference") {
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_keymap 'gb'"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_timezone 'Europe/London'"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("cat >/etc/default/keyboard <<'KBEOF'"));
    }
    
    SECTION("Script ends with cleanup") {
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("rm -f /boot/firstrun.sh"));
        REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("sed -i 's| systemd.run.*||g' /boot/cmdline.txt"));
        REQUIRE(scriptStr.endsWith("exit 0\n"));
    }
}

TEST_CASE("CustomisationGenerator WiFi configuration", "[customization]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "TestNetwork";
    settings["wifiPasswordCrypt"] = "hashed_password_here";
    settings["recommendedWifiCountry"] = "GB";
    settings["wifiHidden"] = true;
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_wlan"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("country=GB"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=\"TestNetwork\""));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("scan_ssid=1"));
    // WPA2/WPA3 transition mode for compatibility with both security types
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("key_mgmt=WPA-PSK SAE"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("psk=hashed_password_here"));
    // PMF optional for WPA3 compatibility
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ieee80211w=1"));
}

TEST_CASE("CustomisationGenerator WiFi configuration with empty PSK (open network)", "[customization]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "OpenNetwork";
    settings["wifiPasswordCrypt"] = "";  // Empty PSK for open network
    settings["recommendedWifiCountry"] = "US";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Open network should use key_mgmt=NONE without psk= line
    // See: https://github.com/raspberrypi/rpi-imager/issues/1396
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=\"OpenNetwork\""));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("key_mgmt=NONE"));
    // Should NOT have WPA-PSK, psk, or ieee80211w for open networks
    REQUIRE_THAT(scriptStr.toStdString(), !ContainsSubstring("WPA-PSK"));
    REQUIRE_THAT(scriptStr.toStdString(), !ContainsSubstring("\tpsk="));
    REQUIRE_THAT(scriptStr.toStdString(), !ContainsSubstring("ieee80211w=1"));
}

TEST_CASE("CustomisationGenerator WiFi country only (no SSID)", "[customization]") {
    QVariantMap settings;
    // Set country code GB without SSID - tests regulatory domain configuration
    settings["recommendedWifiCountry"] = "GB";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Should unblock WiFi even when no SSID is configured
    // This prevents "Wi-Fi is currently blocked by rfkill" message
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("rfkill unblock wifi"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("/var/lib/systemd/rfkill/*:wlan"));
    
    // Should NOT try to configure wpa_supplicant when no SSID
    REQUIRE_THAT(scriptStr.toStdString(), !ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), !ContainsSubstring("set_wlan"));
    
    // Note: The country code "GB" is set via kernel cmdline parameter cfg80211.ieee80211_regdom=GB
    // in imagewriter.cpp (_applySystemdCustomizationFromSettings), not in the firstrun.sh script itself
}

TEST_CASE("CustomisationGenerator Raspberry Pi Connect", "[customization]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["piConnectEnabled"] = true;
    
    QString token = "test-token-12345";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings, token);
    QString scriptStr = QString::fromUtf8(script);
    
    // Check deploy key is written
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring(PI_CONNECT_CONFIG_PATH));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring(PI_CONNECT_DEPLOY_KEY_FILENAME));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("test-token-12345"));
    
    // Check systemd unit directories are created
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("SYSTEMD_USER_BASE="));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("default.target.wants"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("paths.target.wants"));
    
    // Check all three systemd units are enabled
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("rpi-connect.service"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("rpi-connect-signin.path"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("rpi-connect-wayvnc.service"));
    
    // Check systemd linger is set up for auto-start
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("/var/lib/systemd/linger"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("install -m 0644 /dev/null \"/var/lib/systemd/linger/$TARGET_USER\""));
}

// Negative Tests - Testing resilience to invalid/malicious inputs
TEST_CASE("CustomisationGenerator handles empty settings gracefully", "[customization][negative]") {
    QVariantMap settings;  // Completely empty
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Should still generate valid script with header and cleanup
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("#!/bin/sh"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set +e"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("rm -f /boot/firstrun.sh"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("exit 0"));
}

TEST_CASE("CustomisationGenerator handles shell injection attempts in username", "[customization][negative][security]") {
    QVariantMap settings;
    settings["sshUserName"] = "user'; rm -rf /; echo '";
    settings["sshUserPassword"] = "$5$salt$hash";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Username should be shell-quoted, preventing injection
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("'user'\"'\"'; rm -rf /; echo '\"'\"''"));
    // Script should still be valid
    REQUIRE(scriptStr.endsWith("exit 0\n"));
}

TEST_CASE("CustomisationGenerator handles special characters in hostname", "[customization][negative]") {
    QVariantMap settings;
    settings["hostname"] = "test-host_123";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Hostname with dashes and underscores should be handled
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("test-host_123"));
}

TEST_CASE("CustomisationGenerator handles quotes in timezone", "[customization][negative]") {
    QVariantMap settings;
    settings["timezone"] = "Europe/London'; rm -rf /; echo 'pwned";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Timezone should be shell-quoted to prevent injection
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_timezone 'Europe/London'\"'\"'; rm -rf /; echo '\"'\"'pwned'"));
}

TEST_CASE("CustomisationGenerator handles empty password with username", "[customization][negative]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "";  // Empty password
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Should still create user even with empty password
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("userconf-pi/userconf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("'testuser'"));
}

TEST_CASE("CustomisationGenerator handles special characters in WiFi SSID", "[customization][negative]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "Test Network (5GHz)";
    settings["wifiPasswordCrypt"] = "fakehash";
    settings["recommendedWifiCountry"] = "US";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // SSID with parentheses and spaces should work fine
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=\"Test Network (5GHz)\""));
}

TEST_CASE("CustomisationGenerator handles quotes in WiFi SSID", "[customization][negative]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "My \"Quoted\" Network";
    settings["wifiPasswordCrypt"] = "fakehash";
    settings["recommendedWifiCountry"] = "US";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Embedded quotes require hex encoding to preserve exact SSID octets
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=hex:4d79202251756f74656422204e6574776f726b"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_wlan"));
}

TEST_CASE("CustomisationGenerator handles backslashes in WiFi SSID", "[customization][negative]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "Network\\With\\Backslashes";
    settings["wifiPasswordCrypt"] = "fakehash";
    settings["recommendedWifiCountry"] = "US";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Backslashes require hex encoding to preserve exact SSID octets
    REQUIRE_THAT(scriptStr.toStdString(),
                 ContainsSubstring("ssid=hex:4e6574776f726b5c576974685c4261636b736c6173686573"));
}

TEST_CASE("CustomisationGenerator handles non-ASCII UTF-8 WiFi SSID", "[customization][wifi][exotic]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "Café ☕ 日本語";
    settings["wifiPasswordCrypt"] = "fakehash";  // Pre-computed PSK (passwords are ASCII-only per WPA2 spec)
    settings["recommendedWifiCountry"] = "FR";

    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);

    // NOTE: SSIDs support full UTF-8 per WiFi spec. Passwords are ASCII-only (8-63 chars) or
    // pre-computed 64-char hex PSK per WPA2/WPA3 spec. The UI enforces this correctly.
    // This test validates the generator handles UTF-8 SSIDs robustly for:
    // - Edge cases that bypass UI validation
    // - Future WPA standards that may allow UTF-8 passphrases
    // - Programmatic/CLI usage

    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("Café ☕ 日本語"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_wlan"));

    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    const QByteArray escaped = CustomisationGenerator::yamlEscapeSsidOctets(
        settings.value("wifiSSID").toString().toUtf8());
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"" + escaped.toStdString() + "\":"));
}

namespace {

QVariantMap exoticWifiSettings(const QString& ssid,
                               const QString& cryptedPsk = QStringLiteral("fakecryptedhash123"),
                               bool hidden = false)
{
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = ssid;
    settings["wifiPasswordCrypt"] = cryptedPsk;
    settings["recommendedWifiCountry"] = "GB";
    if (hidden)
        settings["wifiHidden"] = true;
    return settings;
}

QVariantMap exoticWifiSettingsFromOctets(const QByteArray& ssidOctets,
                                         const QString& cryptedPsk = QStringLiteral("fakecryptedhash123"),
                                         bool hidden = false)
{
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSsidOctets"] = ssidOctets;
    settings["wifiPasswordCrypt"] = cryptedPsk;
    settings["recommendedWifiCountry"] = "GB";
    if (hidden)
        settings["wifiHidden"] = true;
    return settings;
}

bool wpaUsesHexEncoding(const QByteArray& ssidOctets)
{
    // Materialise the decode (the proxy is lazy) and use Stateless so truncated
    // trailing sequences count as errors — mirrors production isValidUtf8().
    auto converter = QStringDecoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = converter(ssidOctets);
    Q_UNUSED(decoded)
    if (converter.hasError())
        return true;
    for (unsigned char byte : ssidOctets) {
        if (byte < 0x20 || byte == 0x7F || byte == '\\' || byte == '"')
            return true;
    }
    return false;
}

void requireSsidOctetsPreservedInSystemdScript(const QByteArray& ssidOctets)
{
    const QString script = QString::fromUtf8(
        CustomisationGenerator::generateSystemdScript(exoticWifiSettingsFromOctets(ssidOctets)));

    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder(ssidOctets);
    Q_UNUSED(decoded)
    const bool imagerCustomSafe = !ssidOctets.contains('\0') && !decoder.hasError();
    if (imagerCustomSafe) {
        REQUIRE_THAT(script.toStdString(),
                     ContainsSubstring("set_wlan '" + QString::fromUtf8(ssidOctets).toStdString() + "'"));
    } else {
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("imager_custom ] && false"));
    }

    if (wpaUsesHexEncoding(ssidOctets)) {
        REQUIRE_THAT(script.toStdString(),
                     ContainsSubstring("ssid=hex:" + ssidOctets.toHex().toStdString()));
    } else {
        REQUIRE_THAT(script.toStdString(),
                     ContainsSubstring("ssid=\"" + QString::fromUtf8(ssidOctets).toStdString() + "\""));
    }
}

void requireSsidOctetsPreservedInCloudInitYaml(const QByteArray& ssidOctets)
{
    const QString yaml = QString::fromUtf8(
        CustomisationGenerator::generateCloudInitNetworkConfig(exoticWifiSettingsFromOctets(ssidOctets), false));
    const QByteArray escaped = CustomisationGenerator::yamlEscapeSsidOctets(ssidOctets);
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"" + escaped.toStdString() + "\":"));
}

void requireSsidPreservedInSystemdScript(const QString& ssid)
{
    requireSsidOctetsPreservedInSystemdScript(ssid.toUtf8());
}

void requireSsidPreservedInCloudInitYaml(const QString& ssid)
{
    requireSsidOctetsPreservedInCloudInitYaml(ssid.toUtf8());
}

} // namespace

TEST_CASE("CustomisationGenerator handles malformed UTF-8 octets and Unicode homograph edges",
          "[customization][cloudinit][network][wifi][exotic]") {
    // IEEE 802.11 SSIDs are 0-32 opaque octets. Programmatic callers can supply raw
    // bytes via wifiSsidOctets; the UI path UTF-8 encodes the entered text instead.

    SECTION("SSID whose last octet is a UTF-8 continuation byte (0xBF) without a leading byte") {
        QByteArray octets("net", 3);
        octets.append(char(0xBF));
        requireSsidOctetsPreservedInSystemdScript(octets);
        requireSsidOctetsPreservedInCloudInitYaml(octets);
    }

    SECTION("SSID whose last octet is UTF-8 continuation byte 0x80") {
        QByteArray octets("wifi", 4);
        octets.append(char(0x80));
        requireSsidOctetsPreservedInSystemdScript(octets);
        requireSsidOctetsPreservedInCloudInitYaml(octets);
    }

    SECTION("SSID ending in a truncated UTF-8 multibyte sequence (lead byte only)") {
        QByteArray octets("open", 4);
        octets.append(char(0xC3));
        requireSsidOctetsPreservedInSystemdScript(octets);
        requireSsidOctetsPreservedInCloudInitYaml(octets);
    }

    SECTION("SSID ending in a truncated three-byte UTF-8 sequence (Euro without final byte)") {
        QByteArray octets("cost", 4);
        octets.append(char(0xE2));
        octets.append(char(0x82));
        requireSsidOctetsPreservedInSystemdScript(octets);
        requireSsidOctetsPreservedInCloudInitYaml(octets);
    }

    SECTION("32-octet SSID ending in continuation byte 0xBE") {
        QByteArray octets(32, 'X');
        octets[31] = char(0xBE);
        requireSsidOctetsPreservedInSystemdScript(octets);
        requireSsidOctetsPreservedInCloudInitYaml(octets);
    }

    SECTION("NFC and NFD forms of the same visual name are distinct SSIDs") {
        const QString nfc = QStringLiteral("caf\u00E9");       // precomposed é
        const QString nfd = QStringLiteral("cafe\u0301");       // e + combining acute
        REQUIRE(nfc != nfd);
        REQUIRE(nfc.toUtf8() != nfd.toUtf8());

        requireSsidPreservedInSystemdScript(nfc);
        requireSsidPreservedInSystemdScript(nfd);
        requireSsidPreservedInCloudInitYaml(nfc);
        requireSsidPreservedInCloudInitYaml(nfd);
    }

    SECTION("Homoglyph SSIDs using confusable Cyrillic and Latin letters differ") {
        const QString latin = QStringLiteral("AccessPoint");
        const QString homoglyph = QString(QChar(0x0410)) + QStringLiteral("ccessPoint"); // Cyrillic А
        REQUIRE(latin != homoglyph);

        requireSsidPreservedInSystemdScript(latin);
        requireSsidPreservedInSystemdScript(homoglyph);
        requireSsidPreservedInCloudInitYaml(latin);
        requireSsidPreservedInCloudInitYaml(homoglyph);
    }

    SECTION("Incomplete grapheme cluster: lone combining mark without base character") {
        const QString ssid = QString(QChar(0x0301)); // combining acute, no base letter
        REQUIRE(ssid.length() == 1);

        requireSsidPreservedInSystemdScript(ssid);
        requireSsidPreservedInCloudInitYaml(ssid);
    }

    SECTION("Bidirectional override character in SSID") {
        const QString ssid = QStringLiteral("safe") + QChar(0x202E) + QStringLiteral("name");
        requireSsidPreservedInSystemdScript(ssid);
        requireSsidPreservedInCloudInitYaml(ssid);
    }

    SECTION("Legacy PBKDF2 uses exact SSID octets for distinct malformed values") {
        QByteArray truncatedEuro("cost", 4);
        truncatedEuro.append(char(0xE2));
        truncatedEuro.append(char(0x82));

        QByteArray loneLead("x", 1);
        loneLead.append(char(0xC3));
        REQUIRE(truncatedEuro != loneLead);

        QVariantMap settings = exoticWifiSettingsFromOctets(truncatedEuro);
        settings.remove("wifiPasswordCrypt");
        settings["wifiPassword"] = "password1";

        const QString expectedPsk = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha1,
            QByteArray("password1"),
            truncatedEuro,
            4096,
            32).toHex();

        const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("\tpsk=" + expectedPsk.toStdString()));
    }
}

TEST_CASE("CustomisationGenerator handles exotic WiFi SSIDs in systemd script", "[customization][wifi][exotic]") {
    SECTION("SSID beginning with a hyphen") {
        const QString ssid = "-foobar";
        const QString script = QString::fromUtf8(
            CustomisationGenerator::generateSystemdScript(exoticWifiSettings(ssid)));

        REQUIRE_THAT(script.toStdString(), ContainsSubstring("set_wlan '-foobar'"));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("ssid=\"-foobar\""));
    }

    SECTION("SSID beginning with multiple hyphens") {
        const QString ssid = "---hidden-net";
        const QString script = QString::fromUtf8(
            CustomisationGenerator::generateSystemdScript(exoticWifiSettings(ssid)));

        REQUIRE_THAT(script.toStdString(), ContainsSubstring("set_wlan '---hidden-net'"));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("ssid=\"---hidden-net\""));
    }

    SECTION("Hidden network with hyphen-prefixed SSID keeps -h flag separate from SSID") {
        const QString ssid = "-foobar";
        const QString script = QString::fromUtf8(
            CustomisationGenerator::generateSystemdScript(exoticWifiSettings(ssid, "fakehash", true)));

        REQUIRE_THAT(script.toStdString(), ContainsSubstring("set_wlan  -h '-foobar'"));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("scan_ssid=1"));
    }

    SECTION("SSID with emoji, RTL scripts, and combining characters") {
        const QString ssid = QStringLiteral("📶 WiFi שָׁלוֹם مرحبا e\u0301t\u0301");
        requireSsidOctetsPreservedInSystemdScript(ssid.toUtf8());
    }

    SECTION("SSID containing arbitrary non-UTF-8 octets") {
        QByteArray octets;
        octets.append('-');
        octets.append(QByteArray::fromHex("cafe"));
        octets.append(char(0xFF));
        octets.append(char(0x80));
        requireSsidOctetsPreservedInSystemdScript(octets);
    }

    SECTION("SSID at IEEE 802.11 maximum length of 32 octets") {
        QByteArray octets(32, '\0');
        octets[0] = '-';
        octets.replace(1, 31, QByteArray(31, 'A'));
        requireSsidOctetsPreservedInSystemdScript(octets);
    }

    SECTION("Legacy plaintext passphrase beginning with a hyphen") {
        QVariantMap settings = exoticWifiSettings("-network");
        settings.remove("wifiPasswordCrypt");
        settings["wifiPassword"] = "-secretpw";

        const QString expectedPsk = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha1,
            QByteArray("-secretpw"),
            QByteArray("-network"),
            4096,
            32).toHex();

        const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));

        REQUIRE_THAT(script.toStdString(), ContainsSubstring("set_wlan '-network' '" + expectedPsk.toStdString() + "'"));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("\tpsk=" + expectedPsk.toStdString()));
    }

    SECTION("Legacy UTF-8 passphrase with exotic SSID derives deterministic PSK") {
        QVariantMap settings = exoticWifiSettings("Café-📶");
        settings.remove("wifiPasswordCrypt");
        settings["wifiPassword"] = "パスワード123";

        const QString expectedPsk = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha1,
            QString("パスワード123").toUtf8(),
            QString("Café-📶").toUtf8(),
            4096,
            32).toHex();

        const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));

        REQUIRE_THAT(script.toStdString(), ContainsSubstring("set_wlan 'Café-📶' '" + expectedPsk.toStdString() + "'"));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("\tpsk=" + expectedPsk.toStdString()));
    }
}

TEST_CASE("CustomisationGenerator handles exotic WiFi SSIDs in cloud-init network-config",
          "[cloudinit][network][wifi][exotic]") {
    SECTION("SSID beginning with a hyphen") {
        const QString yaml = QString::fromUtf8(
            CustomisationGenerator::generateCloudInitNetworkConfig(exoticWifiSettings("-foobar"), false));

        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"-foobar\":"));
        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("password: \"fakecryptedhash123\""));
    }

    SECTION("SSID beginning with multiple hyphens") {
        const QString yaml = QString::fromUtf8(
            CustomisationGenerator::generateCloudInitNetworkConfig(exoticWifiSettings("---mesh-node"), false));

        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"---mesh-node\":"));
    }

    SECTION("Hidden network with hyphen-prefixed SSID") {
        const QString yaml = QString::fromUtf8(
            CustomisationGenerator::generateCloudInitNetworkConfig(exoticWifiSettings("-foobar", "fakehash", true), false));

        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"-foobar\":"));
        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hidden: true"));
    }

    SECTION("SSID with emoji, RTL scripts, and combining characters") {
        const QString ssid = QStringLiteral("📶 WiFi שָׁלוֹם مرحبا e\u0301t\u0301");
        requireSsidOctetsPreservedInCloudInitYaml(ssid.toUtf8());
    }

    SECTION("SSID containing null and other control octets") {
        QByteArray octets("-net", 4);
        octets.append(char(0x00));
        octets.append(char(0x09));
        octets.append(char(0x01));
        requireSsidOctetsPreservedInCloudInitYaml(octets);
    }

    SECTION("SSID containing arbitrary non-UTF-8 octets") {
        QByteArray octets;
        octets.append('-');
        octets.append(QByteArray::fromHex("cafe"));
        octets.append(char(0xFF));
        requireSsidOctetsPreservedInCloudInitYaml(octets);
    }

    SECTION("SSID at IEEE 802.11 maximum length of 32 octets") {
        QByteArray octets(32, '\0');
        octets[0] = '-';
        octets.replace(1, 31, QByteArray(31, 'B'));
        requireSsidOctetsPreservedInCloudInitYaml(octets);
    }

    SECTION("Legacy plaintext passphrase beginning with a hyphen") {
        QVariantMap settings = exoticWifiSettings("-network");
        settings.remove("wifiPasswordCrypt");
        settings["wifiPassword"] = "-secretpw";

        const QString expectedPsk = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha1,
            QByteArray("-secretpw"),
            QByteArray("-network"),
            4096,
            32).toHex();

        const QString yaml = QString::fromUtf8(
            CustomisationGenerator::generateCloudInitNetworkConfig(settings, false));

        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"-network\":"));
        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("password: \"" + expectedPsk.toStdString() + "\""));
    }

    SECTION("Legacy UTF-8 passphrase with exotic SSID derives deterministic PSK") {
        QVariantMap settings = exoticWifiSettings("Café-📶");
        settings.remove("wifiPasswordCrypt");
        settings["wifiPassword"] = "パスワード123";

        const QString expectedPsk = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha1,
            QString("パスワード123").toUtf8(),
            QString("Café-📶").toUtf8(),
            4096,
            32).toHex();

        const QString yaml = QString::fromUtf8(
            CustomisationGenerator::generateCloudInitNetworkConfig(settings, false));

        const QByteArray escapedKey = CustomisationGenerator::yamlEscapeSsidOctets(QString("Café-📶").toUtf8());
        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"" + escapedKey.toStdString() + "\":"));
        REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("password: \"" + expectedPsk.toStdString() + "\""));
    }
}

TEST_CASE("CustomisationGenerator handles multiline SSH key", "[customization][negative]") {
    QVariantMap settings;
    settings["sshEnabled"] = true;
    settings["sshAuthorizedKeys"] = "ssh-rsa AAAAB3...key1\n\n\nssh-rsa AAAAB3...key2\n\n";  // Extra newlines
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Should handle extra newlines gracefully (Qt::SkipEmptyParts)
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssh-rsa AAAAB3...key1"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssh-rsa AAAAB3...key2"));
}

TEST_CASE("CustomisationGenerator handles SSH public key only (no username/password)", "[customization][ssh]") {
    // Regression test for issue: public key SSH without username/password caused
    // initial setup wizard to appear because userconf wasn't run
    QVariantMap settings;
    settings["sshEnabled"] = true;
    settings["sshPublicKey"] = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestPublicKey user@host";
    // Note: NO sshUserName or sshUserPassword set
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // SSH key should be written
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("cat > \"$FIRSTUSERHOME/.ssh/authorized_keys\" <<'EOF'"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssh-ed25519"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("PasswordAuthentication no"));
    
    // Crucially: userconf should STILL run to mark system as configured
    // This prevents the initial setup wizard from appearing
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("/usr/lib/userconf-pi/userconf"));
    // Should use default 'pi' user when no username specified
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("'pi'"));
}

TEST_CASE("CustomisationGenerator cloud-init handles SSH public key only (no username/password)", "[cloudinit][ssh]") {
    // Regression test for issue: public key SSH without username/password caused
    // users section to be omitted from cloud-init config
    QVariantMap settings;
    settings["sshPublicKey"] = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestPublicKey user@host";
    // Note: NO sshUserName or sshUserPassword set
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // Should generate users section even without explicit username
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("[ systemctl, enable, --now, ssh ]"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: pi"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_authorized_keys:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh-ed25519"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("lock_passwd: true"));
    // SSH keys alone should NOT grant passwordless sudo — requires explicit opt-in
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("sudo: ALL=(ALL) NOPASSWD:ALL"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  sudo: null"));
}

TEST_CASE("CustomisationGenerator handles multiple SSH keys in .pub file", "[customization][ssh]") {
    // Test that .pub files with multiple keys (one per line) are properly split
    QVariantMap settings;
    settings["sshEnabled"] = true;
    settings["sshPublicKey"] = "ssh-rsa AAAAB3...key1 user@host1\nssh-ed25519 AAAAC3...key2 user@host2";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Both keys should be written separately
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("cat > \"$FIRSTUSERHOME/.ssh/authorized_keys\" <<'EOF'"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssh-rsa AAAAB3...key1"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssh-ed25519 AAAAC3...key2"));
    // Keys should be on separate lines (joined with \n)
    int key1Pos = scriptStr.indexOf("ssh-rsa AAAAB3...key1");
    int key2Pos = scriptStr.indexOf("ssh-ed25519 AAAAC3...key2");
    REQUIRE(key1Pos != -1);
    REQUIRE(key2Pos != -1);
    REQUIRE(key2Pos > key1Pos);
}

TEST_CASE("CustomisationGenerator cloud-init handles multiple SSH keys in .pub file", "[cloudinit][ssh]") {
    // Test that .pub files with multiple keys are properly split for cloud-init
    QVariantMap settings;
    settings["sshPublicKey"] = "ssh-rsa AAAAB3...key1 user@host1\nssh-ed25519 AAAAC3...key2 user@host2";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // Both keys should be in separate YAML list items
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_authorized_keys:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- \"ssh-rsa AAAAB3...key1 user@host1\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- \"ssh-ed25519 AAAAC3...key2 user@host2\""));
}

TEST_CASE("CustomisationGenerator cloud-init quotes SSH keys with YAML-special characters", "[cloudinit][ssh]") {
    // Regression test for https://github.com/raspberrypi/rpi-imager/issues/1544
    // SSH keys with a colon in the comment (e.g. "ssh:") were emitted unquoted,
    // causing YAML to interpret them as mapping keys instead of strings.
    QVariantMap settings;
    settings["sshPublicKey"] = "sk-ssh-ed25519@openssh.com AAAAGnNr...DMtkAAAABHNzaDo= ssh:";

    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "pi");
    QString yaml = QString::fromUtf8(userdata);

    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_authorized_keys:"));
    // Key must be quoted to prevent YAML from interpreting "ssh:" as a mapping key
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- \"sk-ssh-ed25519@openssh.com AAAAGnNr...DMtkAAAABHNzaDo= ssh:\""));
}

TEST_CASE("CustomisationGenerator handles very long hostname", "[customization][negative]") {
    QVariantMap settings;
    // Hostnames should be max 63 chars, but test we don't crash with longer
    settings["hostname"] = QString("verylonghostnameverylonghostnameverylonghostnameverylonghostnameverylonghost");
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Should still generate script without crashing
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_hostname"));
    REQUIRE(scriptStr.endsWith("exit 0\n"));
}

TEST_CASE("CustomisationGenerator handles null/empty piConnect token", "[customization][negative]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["piConnectEnabled"] = true;
    
    QString emptyToken = "";  // Empty token
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings, emptyToken);
    QString scriptStr = QString::fromUtf8(script);
    
    // Should not include Pi Connect setup if token is empty
    REQUIRE_FALSE(scriptStr.contains(PI_CONNECT_CONFIG_PATH));
    REQUIRE_FALSE(scriptStr.contains(PI_CONNECT_DEPLOY_KEY_FILENAME));
}

TEST_CASE("CustomisationGenerator handles invalid keyboard layout", "[customization][negative]") {
    QVariantMap settings;
    settings["keyboard"] = "invalid_layout_xyz";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Should still attempt to set it (validation happens at OS level)
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("XKBLAYOUT=\"invalid_layout_xyz\""));
}

TEST_CASE("CustomisationGenerator handles backticks in username (command substitution attempt)", "[customization][negative][security]") {
    QVariantMap settings;
    settings["sshUserName"] = "user`whoami`";
    settings["sshUserPassword"] = "$5$salt$hash";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Backticks should be safely quoted
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("'user`whoami`'"));
}

TEST_CASE("CustomisationGenerator handles dollar signs in password (variable expansion attempt)", "[customization][negative][security]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$5$salt$(rm -rf /)hash";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Password should be shell-quoted
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("'$5$salt$(rm -rf /)hash'"));
}

// Cloud-init Tests
TEST_CASE("CustomisationGenerator generates cloud-init user-data with hostname", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["hostname"] = "testpi";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings);
    QString yaml = QString::fromUtf8(userdata);
    
    // Don't let cloud-init manage DNS
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("manage_resolv_conf: false"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hostname: testpi"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("manage_etc_hosts: true"));
    // Note: preserve_hostname is NOT set - cloud-init's per-instance behavior
    // (via unique instance-id) ensures hostname is only set once
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("preserve_hostname"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("packages:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- avahi-daemon"));
    // Preserve user's apt sources list
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("apt:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("preserve_sources_list: true"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with timezone", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["timezone"] = "Europe/London";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings);
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("timezone: Europe/London"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with keyboard layout", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["keyboard"] = "gb";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings);
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("model: pc105"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("layout: \"gb\""));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with SSH user", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$5$fakesalt$fakehash123";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "testuser");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("[ systemctl, enable, --now, ssh ]"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: testuser"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("shell: /bin/bash"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("lock_passwd: false"));
    // Password hash should be quoted for proper YAML parsing
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("passwd: \"$5$fakesalt$fakehash123\""));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with user credentials but NO SSH", "[cloudinit][userdata]") {
    // Regression test: user configuration must be independent of SSH settings
    // A user should be able to configure a local account without enabling SSH
    QVariantMap settings;
    settings["sshUserName"] = "localuser";
    settings["sshUserPassword"] = "$5$fakesalt$fakehash456";
    // Note: sshEnabled is NOT set (defaults to false)
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // User configuration MUST be generated even without SSH
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: localuser"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("shell: /bin/bash"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("lock_passwd: false"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("passwd: \"$5$fakesalt$fakehash456\""));
    
    // SSH should NOT be enabled
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("ssh_pwauth:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("ssh_authorized_keys:"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with SSH keys", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshAuthorizedKeys"] = "ssh-rsa AAAAB3...key1\nssh-rsa AAAAB3...key2";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "testuser");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("[ systemctl, enable, --now, ssh ]"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_authorized_keys:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- \"ssh-rsa AAAAB3...key1\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- \"ssh-rsa AAAAB3...key2\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("lock_passwd: true"));
    // SSH keys alone should NOT grant passwordless sudo — requires explicit opt-in
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("sudo: ALL=(ALL) NOPASSWD:ALL"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  sudo: null"));
    // Password authentication should be explicitly disabled when using public-key auth
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_pwauth: false"));
}

TEST_CASE("CustomisationGenerator cloud-init passwordless sudo when explicitly enabled", "[cloudinit][userdata][sudo]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$y$j9T$test$hash";
    settings["passwordlessSudo"] = true;

    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "testuser");
    QString yaml = QString::fromUtf8(userdata);

    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: testuser"));
    // sudo: user property for standard cloud-init
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("sudo: ALL=(ALL) NOPASSWD:ALL"));
    // runcmd fallback for implementations that don't process sudo: user property
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("runcmd:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("testuser ALL=(ALL) NOPASSWD:ALL"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("/etc/sudoers.d/010_testuser-nopasswd"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("chmod"));
    // The opt-in must not also emit the suppressing key
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("sudo: null"));
}

TEST_CASE("CustomisationGenerator cloud-init no passwordless sudo by default", "[cloudinit][userdata][sudo]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$y$j9T$test$hash";

    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "testuser");
    QString yaml = QString::fromUtf8(userdata);

    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: testuser"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("sudo: ALL=(ALL) NOPASSWD:ALL"));
    // Regression test: the singular `user:` block is merged over the distro's
    // default_user from /etc/cloud/cloud.cfg, which carries
    // `sudo: ["ALL=(ALL) NOPASSWD:ALL"]` on every variant including
    // raspberry-pi-os. Silence alone therefore inherits passwordless sudo (via
    // /etc/sudoers.d/90-cloud-init-users), so the key must be set to null.
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  sudo: null"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("/etc/sudoers.d/010_testuser-nopasswd"));
}

TEST_CASE("CustomisationGenerator systemd script passwordless sudo", "[customization][sudo]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$y$j9T$test$hash";
    settings["passwordlessSudo"] = true;

    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString s = QString::fromUtf8(script);

    REQUIRE_THAT(s.toStdString(), ContainsSubstring("testuser ALL=(ALL) NOPASSWD:ALL"));
    REQUIRE_THAT(s.toStdString(), ContainsSubstring("/etc/sudoers.d/010_testuser-nopasswd"));
    REQUIRE_THAT(s.toStdString(), ContainsSubstring("chmod 0440"));
}

TEST_CASE("CustomisationGenerator systemd script no passwordless sudo by default", "[customization][sudo]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$y$j9T$test$hash";

    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString s = QString::fromUtf8(script);

    REQUIRE_THAT(s.toStdString(), !ContainsSubstring("NOPASSWD"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with password auth", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["sshPasswordAuth"] = true;
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "testuser");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_pwauth: true"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with password auth AND SSH keys", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshPasswordAuth"] = true;
    settings["sshAuthorizedKeys"] = "ssh-rsa AAAAB3...key1";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "testuser");
    QString yaml = QString::fromUtf8(userdata);
    
    // Both SSH keys and password auth enabled - password auth takes precedence
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_authorized_keys:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_pwauth: true"));
    // Should NOT contain ssh_pwauth: false
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("ssh_pwauth: false"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with Raspberry Pi interfaces", "[cloudinit][userdata][rpi]") {
    QVariantMap settings;
    settings["enableI2C"] = true;
    settings["enableSPI"] = true;
    settings["enableSerial"] = "Console & Hardware";
    settings["enableUsbGadget"] = true;
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, false, QString());
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("enable_usb_gadget: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("interfaces:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("i2c: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("spi: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("serial:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("console: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hardware: true"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with Pi Connect token", "[cloudinit][userdata][piconnect]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["piConnectEnabled"] = true;
    
    QString token = "test-token-abcd-1234";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, token, false, true, "testuser");
    QString yaml = QString::fromUtf8(userdata);
    
    // Check runcmd section exists (Pi Connect uses runcmd to ensure user exists first)
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("runcmd:"));
    
    // Check config directory is created
    QString expectedInstallDir = QString("install -o testuser -m 700 -d /home/testuser/") + PI_CONNECT_CONFIG_PATH;
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring(expectedInstallDir.toStdString()));
    
    // Check deploy key file is written via printf in runcmd (not write_files)
    // This approach is used because cloud-init tries to resolve user/group at parse time
    // with write_files defer:true, which fails if the user doesn't exist yet
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("printf"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("test-token-abcd-1234"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring(PI_CONNECT_DEPLOY_KEY_FILENAME));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("chmod 600"));
    
    // Check systemd unit directories are created
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring(".config/systemd/user/default.target.wants"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring(".config/systemd/user/paths.target.wants"));
    
    // Check all three systemd units are enabled via symlinks with fallback logic
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("UNIT_SRC=/usr/lib/systemd/user/rpi-connect.service"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("UNIT_SRC=/lib/systemd/user/rpi-connect.service"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ln -sf $UNIT_SRC"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi-connect.service"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi-connect-signin.path"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi-connect-wayvnc.service"));
    
    // Check ownership is set correctly
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("chown -R testuser:testuser"));
    
    // Check systemd linger is set up for auto-start
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("/var/lib/systemd/linger"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("install -m 0644 /dev/null /var/lib/systemd/linger/testuser"));
}

TEST_CASE("CustomisationGenerator generates cloud-init network-config with WiFi", "[cloudinit][network]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "TestNetwork";
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    settings["recommendedWifiCountry"] = "DE";
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("network:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("version: 2"));
    
    // eth0 with DHCP v4 and v6 always present
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ethernets:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("eth0:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("dhcp6: true"));
    
    // WiFi configuration
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("wifis:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("wlan0:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("dhcp4: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("regulatory-domain: \"DE\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("access-points:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"TestNetwork\":"));
    // Use password shorthand (not auth: block) for automatic WPA2/WPA3 transition mode
    // See: https://github.com/canonical/netplan/blob/main/src/parse.c (handle_access_point_password)
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("password: \"fakecryptedhash123\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("optional: true"));
}

TEST_CASE("CustomisationGenerator generates cloud-init network-config with hidden WiFi", "[cloudinit][network]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "HiddenNetwork";
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    settings["wifiHidden"] = true;
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    // eth0 with DHCP v4 and v6 always present
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ethernets:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("eth0:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("dhcp4: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("dhcp6: true"));
    
    // Hidden WiFi configuration
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"HiddenNetwork\":"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hidden: true"));
    // Use password shorthand (not auth: block) for automatic WPA2/WPA3 transition mode
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("password:"));
}

TEST_CASE("CustomisationGenerator generates cloud-init network-config for open WiFi (no password)", "[cloudinit][network]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "OpenNetwork";
    settings["wifiPasswordCrypt"] = "";  // Empty = open network
    settings["recommendedWifiCountry"] = "US";
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    // eth0 with DHCP v4 and v6 always present
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ethernets:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("eth0:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("dhcp4: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("dhcp6: true"));
    
    // Open network configuration - must use auth: key-management: none
    // See: https://github.com/raspberrypi/rpi-imager/issues/1396
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("wifis:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("wlan0:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"OpenNetwork\":"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("regulatory-domain: \"US\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("auth:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("key-management: none"));
    // Should NOT have password field for open networks
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("password:"));
}

TEST_CASE("CustomisationGenerator cloud-init WiFi country only (no SSID)", "[cloudinit][network]") {
    QVariantMap settings;
    // Set country code FR without SSID - tests regulatory domain configuration
    settings["recommendedWifiCountry"] = "FR";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, "", false, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // Should include runcmd to unblock WiFi
    // This prevents "Wi-Fi is currently blocked by rfkill" message
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("runcmd:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rfkill, unblock, wifi"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("/var/lib/systemd/rfkill/*:wlan"));
    
    // A country code alone cannot produce a wifis: block — cloud-init requires at
    // least one access-point — and without one there is nothing to write, so no
    // network-config is emitted. The regulatory domain is applied via the cmdline
    // parameter (cfg80211.ieee80211_regdom) instead.
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    REQUIRE(netcfg.isEmpty());
}

TEST_CASE("CustomisationGenerator generates cloud-init network-config with special characters in SSID", "[cloudinit][network][negative]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "Test \"Network\" (5GHz)";
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    // eth0 with DHCP always present
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ethernets:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("eth0:"));
    
    // Quotes should be escaped
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("Test \\\"Network\\\" (5GHz)"));
    // Use password shorthand (not auth: block) for automatic WPA2/WPA3 transition mode
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("password:"));
}

TEST_CASE("CustomisationGenerator cloud-init network-config escapes backslashes in SSID", "[cloudinit][network][negative]") {
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "Network\\With\\Backslashes";
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    // Backslashes must be escaped in YAML double-quoted strings
    // Per IEEE 802.11, SSIDs can contain any byte including backslashes
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("Network\\\\With\\\\Backslashes"));
}

TEST_CASE("CustomisationGenerator cloud-init network-config escapes control characters in SSID", "[cloudinit][network][negative]") {
    QVariantMap settings;
    // SSID with tab, newline, and carriage return (valid per IEEE 802.11)
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = QString("Net\twork\nWith\rControl");
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    // Control characters must be escaped in YAML double-quoted strings
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("Net\\twork\\nWith\\rControl"));
}

TEST_CASE("CustomisationGenerator cloud-init network-config escapes mixed special characters in SSID", "[cloudinit][network][negative]") {
    QVariantMap settings;
    // Pathological SSID: quotes, backslashes, and control chars together
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = QString("Test\\\"Net\twork\"");
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    // All special characters must be properly escaped
    // Input: Test\"Net<tab>work"
    // Expected YAML escape: Test\\\"Net\twork\"
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("Test\\\\\\\"Net\\twork\\\""));
}

// =============================================================================
// INDEPENDENT STEP TESTS
// =============================================================================
// Each customization step must be able to generate configuration independently
// of all other steps. These tests verify that enabling only one step produces
// the correct output without requiring other steps to be configured.
// =============================================================================

TEST_CASE("Independent step: Hostname only", "[cloudinit][independent][hostname]") {
    // Hostname step configured alone - no other customization
    QVariantMap settings;
    settings["hostname"] = "mypi";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(userdata);
    
    // Hostname configuration MUST be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("manage_resolv_conf: false"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hostname: mypi"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("manage_etc_hosts: true"));
    // preserve_hostname is NOT set - per-instance behavior handles this
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("preserve_hostname"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("packages:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- avahi-daemon"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("apt:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("preserve_sources_list: true"));
    
    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("timezone:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("rpi:"));
    
    // No Wi-Fi here, so no network-config is emitted at all. eth0 DHCP is only
    // written alongside a wifis: block, because a network-config file replaces
    // the distro default and would otherwise take wired ethernet with it.
    REQUIRE(netcfg.isEmpty());
}

TEST_CASE("Independent step: Timezone only", "[cloudinit][independent][locale]") {
    // Locale step with only timezone configured
    QVariantMap settings;
    settings["timezone"] = "America/New_York";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(userdata);
    
    // Timezone configuration MUST be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("timezone: America/New_York"));
    
    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("rpi:"));
    
    // No Wi-Fi here, so no network-config is emitted at all. eth0 DHCP is only
    // written alongside a wifis: block, because a network-config file replaces
    // the distro default and would otherwise take wired ethernet with it.
    REQUIRE(netcfg.isEmpty());
}

TEST_CASE("Independent step: Keyboard only", "[cloudinit][independent][locale]") {
    // Locale step with only keyboard configured
    QVariantMap settings;
    settings["keyboard"] = "de";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(userdata);
    
    // Keyboard configuration MUST be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("model: pc105"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("layout: \"de\""));
    
    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("timezone:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("rpi:"));
    
    // No Wi-Fi here, so no network-config is emitted at all. eth0 DHCP is only
    // written alongside a wifis: block, because a network-config file replaces
    // the distro default and would otherwise take wired ethernet with it.
    REQUIRE(netcfg.isEmpty());
}

TEST_CASE("Independent step: Locale (timezone + keyboard)", "[cloudinit][independent][locale]") {
    // Full locale step: timezone and keyboard together
    QVariantMap settings;
    settings["timezone"] = "Europe/Paris";
    settings["keyboard"] = "fr";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // Both locale settings MUST be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("timezone: Europe/Paris"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("layout: \"fr\""));
    
    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("rpi:"));
}

TEST_CASE("Independent step: User credentials only (no SSH)", "[cloudinit][independent][user]") {
    // User step configured alone - username and password without SSH
    QVariantMap settings;
    settings["sshUserName"] = "alice";
    settings["sshUserPassword"] = "$6$rounds=4096$salt$hashvalue";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(userdata);
    
    // User configuration MUST be generated independently of SSH
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: alice"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("shell: /bin/bash"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("lock_passwd: false"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("passwd: \"$6$rounds=4096$salt$hashvalue\""));
    
    // SSH must NOT be enabled
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("ssh_pwauth:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("ssh_authorized_keys:"));
    
    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("timezone:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("rpi:"));
    
    // No Wi-Fi here, so no network-config is emitted at all. eth0 DHCP is only
    // written alongside a wifis: block, because a network-config file replaces
    // the distro default and would otherwise take wired ethernet with it.
    REQUIRE(netcfg.isEmpty());
}

TEST_CASE("Independent step: WiFi only", "[cloudinit][independent][wifi]") {
    // WiFi step configured alone - no other customization
    QVariantMap settings;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "MyHomeNetwork";
    settings["wifiPasswordCrypt"] = "hashedwifipassword123";
    settings["recommendedWifiCountry"] = "GB";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString userdataYaml = QString::fromUtf8(userdata);
    QString netcfgYaml = QString::fromUtf8(netcfg);
    
    // Network config MUST have eth0 with DHCP
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("network:"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("version: 2"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("ethernets:"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("eth0:"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("dhcp4: true"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("dhcp6: true"));
    
    // Network config MUST have WiFi configured
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("wifis:"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("wlan0:"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("\"MyHomeNetwork\":"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("password: \"hashedwifipassword123\""));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("regulatory-domain: \"GB\""));
    
    // No other customization in userdata
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("user:"));
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("timezone:"));
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("keyboard:"));
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("rpi:"));
}

TEST_CASE("Independent step: SSH with password auth only", "[cloudinit][independent][ssh]") {
    // Remote access step with SSH password auth enabled, no user credentials
    QVariantMap settings;
    settings["sshPasswordAuth"] = true;
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(userdata);
    
    // SSH configuration MUST be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("[ systemctl, enable, --now, ssh ]"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_pwauth: true"));
    
    // No user section without credentials
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("user:"));
    
    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("timezone:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("rpi:"));
    
    // No Wi-Fi here, so no network-config is emitted at all. eth0 DHCP is only
    // written alongside a wifis: block, because a network-config file replaces
    // the distro default and would otherwise take wired ethernet with it.
    REQUIRE(netcfg.isEmpty());
}

TEST_CASE("Independent step: SSH with public keys only", "[cloudinit][independent][ssh]") {
    // Remote access step with SSH public key auth, no password
    QVariantMap settings;
    settings["sshAuthorizedKeys"] = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIG... user@host";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "defaultuser");
    QString yaml = QString::fromUtf8(userdata);
    
    // SSH configuration MUST be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("[ systemctl, enable, --now, ssh ]"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_pwauth: false"));
    
    // User section is created for SSH key deployment (using currentUser fallback)
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: defaultuser"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_authorized_keys:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- \"ssh-ed25519"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("lock_passwd: true"));

    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("timezone:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("rpi:"));
}

TEST_CASE("Independent step: Interfaces only (I2C)", "[cloudinit][independent][interfaces]") {
    // Interfaces & Features step with only I2C enabled
    QVariantMap settings;
    settings["enableI2C"] = true;
    settings["enableSerial"] = "Disabled";  // Explicitly disable to test I2C in isolation
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(userdata);
    
    // Interface configuration MUST be generated (requires hasCcRpi=true)
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("interfaces:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("i2c: true"));
    
    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("timezone:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("keyboard:"));
    
    // No Wi-Fi here, so no network-config is emitted at all. eth0 DHCP is only
    // written alongside a wifis: block, because a network-config file replaces
    // the distro default and would otherwise take wired ethernet with it.
    REQUIRE(netcfg.isEmpty());
}

TEST_CASE("Independent step: Interfaces only (SPI)", "[cloudinit][independent][interfaces]") {
    QVariantMap settings;
    settings["enableSPI"] = true;
    settings["enableSerial"] = "Disabled";  // Explicitly disable to test SPI in isolation
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("interfaces:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("spi: true"));
    
    // No other interfaces enabled
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("i2c:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("onewire:"));
}

TEST_CASE("Independent step: Interfaces only (1-Wire)", "[cloudinit][independent][interfaces]") {
    QVariantMap settings;
    settings["enable1Wire"] = true;
    settings["enableSerial"] = "Disabled";  // Explicitly disable to test 1-Wire in isolation
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("interfaces:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("onewire: true"));
}

TEST_CASE("Independent step: Interfaces only (Serial)", "[cloudinit][independent][interfaces]") {
    QVariantMap settings;
    settings["enableSerial"] = "Console";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("interfaces:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("serial:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("console: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hardware: false"));
}

TEST_CASE("Independent step: USB Gadget only", "[cloudinit][independent][features]") {
    QVariantMap settings;
    settings["enableUsbGadget"] = true;
    // Explicitly disable serial to avoid default behavior
    settings["enableSerial"] = "Disabled";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("enable_usb_gadget: true"));
    
    // No interfaces section when only USB gadget is enabled (and serial explicitly disabled)
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("interfaces:"));
}

TEST_CASE("Independent step: Pi Connect only (with required user)", "[cloudinit][independent][piconnect]") {
    // Pi Connect requires a user to be configured for the token file ownership
    // But Pi Connect step itself should work without other steps
    QVariantMap settings;
    settings["sshUserName"] = "connectuser";
    settings["sshUserPassword"] = "$5$salt$hash";
    settings["piConnectEnabled"] = true;
    
    QString token = "pi-connect-deploy-token-xyz";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, token, false, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(userdata);
    
    // User configuration MUST be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: connectuser"));
    
    // Pi Connect configuration MUST be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("runcmd:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("pi-connect-deploy-token-xyz"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring(PI_CONNECT_CONFIG_PATH));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("install -o connectuser"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("chown"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi-connect.service"));
    
    // No other customization should be present
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("hostname:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("timezone:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("keyboard:"));
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("rpi:"));
    
    // No Wi-Fi here, so no network-config is emitted at all. eth0 DHCP is only
    // written alongside a wifis: block, because a network-config file replaces
    // the distro default and would otherwise take wired ethernet with it.
    REQUIRE(netcfg.isEmpty());
}

// =============================================================================
// COMBINATION TESTS: Verify steps don't interfere with each other
// =============================================================================

TEST_CASE("Combined steps: User + Hostname (no SSH)", "[cloudinit][combined]") {
    // User and hostname configured together, but SSH disabled
    QVariantMap settings;
    settings["hostname"] = "workstation";
    settings["sshUserName"] = "developer";
    settings["sshUserPassword"] = "$6$salt$hash";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // Both must be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hostname: workstation"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: developer"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("passwd:"));
    
    // SSH not enabled
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
}

TEST_CASE("Combined steps: User + WiFi (no SSH)", "[cloudinit][combined]") {
    // User credentials and WiFi configured, but SSH disabled
    QVariantMap settings;
    settings["sshUserName"] = "wifiuser";
    settings["sshUserPassword"] = "$6$salt$hash";
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "OfficeWiFi";
    settings["wifiPasswordCrypt"] = "wifihash";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString userdataYaml = QString::fromUtf8(userdata);
    QString netcfgYaml = QString::fromUtf8(netcfg);
    
    // User config must be generated
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("  name: wifiuser"));
    
    // WiFi config must be generated
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("\"OfficeWiFi\":"));
    
    // SSH not enabled
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("enable_ssh:"));
}

TEST_CASE("Combined steps: All locale + User (no SSH)", "[cloudinit][combined]") {
    // Full locale customization with user, but no SSH
    QVariantMap settings;
    settings["timezone"] = "Asia/Tokyo";
    settings["keyboard"] = "jp";
    settings["sshUserName"] = "jpuser";
    settings["sshUserPassword"] = "$6$salt$hash";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // All must be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("timezone: Asia/Tokyo"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("layout: \"jp\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: jpuser"));
    
    // SSH not enabled
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
}

TEST_CASE("Combined steps: User + Interfaces (no SSH)", "[cloudinit][combined]") {
    // User and hardware interfaces, no SSH
    QVariantMap settings;
    settings["sshUserName"] = "iotuser";
    settings["sshUserPassword"] = "$6$salt$hash";
    settings["enableI2C"] = true;
    settings["enableSPI"] = true;
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // User must be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("  name: iotuser"));
    
    // Interfaces must be generated
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("i2c: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("spi: true"));
    
    // SSH not enabled
    REQUIRE_THAT(yaml.toStdString(), !ContainsSubstring("enable_ssh:"));
}

TEST_CASE("Combined steps: Full customization without SSH", "[cloudinit][combined]") {
    // Everything except SSH - verifies user config works independently
    QVariantMap settings;
    settings["hostname"] = "fullpi";
    settings["timezone"] = "Europe/Berlin";
    settings["keyboard"] = "de";
    settings["sshUserName"] = "fulluser";
    settings["sshUserPassword"] = "$6$salt$hash";
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "FullWiFi";
    settings["wifiPasswordCrypt"] = "wifihash";
    settings["recommendedWifiCountry"] = "DE";
    settings["enableI2C"] = true;
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, false, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString userdataYaml = QString::fromUtf8(userdata);
    QString netcfgYaml = QString::fromUtf8(netcfg);
    
    // All user-data customizations must be present
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("hostname: fullpi"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("timezone: Europe/Berlin"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("layout: \"de\""));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("  name: fulluser"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("passwd:"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("i2c: true"));
    
    // WiFi in network config
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("\"FullWiFi\":"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("regulatory-domain: \"DE\""));
    
    // SSH explicitly NOT enabled
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("enable_ssh:"));
    REQUIRE_THAT(userdataYaml.toStdString(), !ContainsSubstring("ssh_pwauth:"));
}

TEST_CASE("Combined steps: Full customization with SSH", "[cloudinit][combined]") {
    // Full customization including SSH
    QVariantMap settings;
    settings["hostname"] = "sshpi";
    settings["timezone"] = "UTC";
    settings["keyboard"] = "us";
    settings["sshUserName"] = "sshuser";
    settings["sshUserPassword"] = "$6$salt$hash";
    settings["sshPasswordAuth"] = true;
    settings["wifiConfigured"] = true;
    settings["wifiSSID"] = "SSHWiFi";
    settings["wifiPasswordCrypt"] = "wifihash";
    settings["enableSPI"] = true;
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), true, true, "pi");
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString userdataYaml = QString::fromUtf8(userdata);
    QString netcfgYaml = QString::fromUtf8(netcfg);
    
    // All user-data customizations must be present
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("hostname: sshpi"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("timezone: UTC"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("layout: \"us\""));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("user:"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("  name: sshuser"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("passwd:"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("rpi:"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("spi: true"));
    
    // SSH configuration must be present
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("[ systemctl, enable, --now, ssh ]"));
    REQUIRE_THAT(userdataYaml.toStdString(), ContainsSubstring("ssh_pwauth: true"));
    
    // WiFi in network config
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("\"SSHWiFi\":"));
}

// =============================================================================
// END OF INDEPENDENT STEP TESTS
// =============================================================================

TEST_CASE("CustomisationGenerator handles empty cloud-init settings gracefully", "[cloudinit][negative]") {
    QVariantMap settings;  // Empty settings
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings);
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings);

    // Nothing configured means nothing written. Both generators used to emit a
    // baseline (manage_resolv_conf, and eth0 DHCP) unconditionally, which made
    // the fastboot and download paths write meta-data/network-config even when
    // the user had skipped customisation — and older fastboot gadgets failed on
    // that write. An empty payload is what tells those paths to skip the file.
    REQUIRE(userdata.isEmpty());
    REQUIRE(netcfg.isEmpty());
}

TEST_CASE("CustomisationGenerator cloud-init handles empty Pi Connect token", "[cloudinit][negative]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["piConnectEnabled"] = true;
    
    QString emptyToken = "";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, emptyToken, false, true, "testuser");
    QString yaml = QString::fromUtf8(userdata);
    
    // Should not include write_files or runcmd for Pi Connect
    REQUIRE_FALSE(yaml.contains("write_files:"));
    REQUIRE_FALSE(yaml.contains(PI_CONNECT_CONFIG_PATH));
}

// =============================================================================
// rpi-preseed.toml serialiser
// =============================================================================

TEST_CASE("rpi-preseed empty settings produce no file", "[preseed][negative]") {
    QVariantMap settings;  // Nothing configured
    QByteArray toml = CustomisationGenerator::generateRpiPreseedToml(settings);
    REQUIRE(toml.isEmpty());
}

TEST_CASE("rpi-preseed emits config_version header and system section", "[preseed]") {
    QVariantMap settings;
    settings["hostname"] = "cm5-jig";

    QByteArray toml = CustomisationGenerator::generateRpiPreseedToml(settings);
    std::string s = QString::fromUtf8(toml).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("config_version = \"1.0\""));
    REQUIRE_THAT(s, ContainsSubstring("[system]"));
    REQUIRE_THAT(s, ContainsSubstring("hostname = \"cm5-jig\""));
    // Should not leave a trailing blank line pair.
    REQUIRE_FALSE(QString::fromUtf8(toml).endsWith("\n\n"));
}

TEST_CASE("rpi-preseed user section marks pre-hashed password encrypted", "[preseed][user]") {
    QVariantMap settings;
    settings["sshUserName"] = "jig";
    settings["sshUserPassword"] = "$5$abc$def";  // crypted hash from the wizard
    settings["passwordlessSudo"] = true;

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("[user]"));
    REQUIRE_THAT(s, ContainsSubstring("name = \"jig\""));
    REQUIRE_THAT(s, ContainsSubstring("password = \"$5$abc$def\""));
    REQUIRE_THAT(s, ContainsSubstring("password_encrypted = true"));
    REQUIRE_THAT(s, ContainsSubstring("groups = [\"sudo\"]"));
    REQUIRE_THAT(s, ContainsSubstring("passwordless_sudo = true"));
}

TEST_CASE("rpi-preseed user without password omits password keys", "[preseed][user]") {
    QVariantMap settings;
    settings["sshUserName"] = "jig";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("name = \"jig\""));
    REQUIRE_THAT(s, !ContainsSubstring("password ="));
    REQUIRE_THAT(s, !ContainsSubstring("passwordless_sudo"));
    // The account is always made a sudoer (parity with the admin default),
    // independent of whether a password or passwordless sudo is configured.
    REQUIRE_THAT(s, ContainsSubstring("groups = [\"sudo\"]"));
}

// The two characters TOML gives meaning to, in values a user types.
//
// rpi-preseed's parser unescapes exactly \\ and \", so a value carrying
// either has to arrive escaped. Wi-Fi passphrases are where this bites: a
// quote in one is ordinary, and an unescaped quote ends the string early --
// the rest of the line becomes syntax the parser rejects, the file is
// discarded, and the board comes up with no network, no user and no reason
// given. Neither character had ever been through this serialiser.

TEST_CASE("rpi-preseed escapes a double quote in a value", "[preseed][quoting]") {
    QVariantMap settings;
    settings["hostname"] = "the\"pi";
    settings["sshUserName"] = "jig";
    settings["sshUserPassword"] = "pa\"ss";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    INFO(s);
    REQUIRE_THAT(s, ContainsSubstring("hostname = \"the\\\"pi\""));
    REQUIRE_THAT(s, ContainsSubstring("password = \"pa\\\"ss\""));
}

TEST_CASE("rpi-preseed escapes a backslash in a value", "[preseed][quoting]") {
    // A backslash left alone would be read as the start of an escape, so
    // "pa\ss" becomes an unknown escape or eats the character after it.
    QVariantMap settings;
    settings["hostname"] = "the\\pi";
    settings["sshUserName"] = "jig";
    settings["sshUserPassword"] = "pa\\ss";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    INFO(s);
    REQUIRE_THAT(s, ContainsSubstring("hostname = \"the\\\\pi\""));
    REQUIRE_THAT(s, ContainsSubstring("password = \"pa\\\\ss\""));
}

TEST_CASE("rpi-preseed escapes a Wi-Fi passphrase carrying both", "[preseed][quoting][wifi]") {
    // The realistic one. A passphrase is chosen for entropy, not for what a
    // configuration format finds convenient.
    QVariantMap settings;
    settings["wifiSSID"] = "home\"net";
    settings["wifiPassword"] = "a\"b\\c";
    settings["wifiCountry"] = "GB";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    INFO(s);
    REQUIRE_THAT(s, ContainsSubstring("ssid = \"home\\\"net\""));
    // Escaped, and left as a passphrase rather than mistaken for a raw key.
    REQUIRE_THAT(s, ContainsSubstring("a\\\"b\\\\c"));
}

TEST_CASE("rpi-preseed tells a stored key from a passphrase by its digits",
          "[preseed][quoting][wifi]") {
    // A legacy setting holds either a 64-character hex PMK or whatever the
    // user typed. Called wrong in one direction the key is derived a second
    // time from something that is already a key; in the other a passphrase
    // is written as if it were one. Both come out as a board that will not
    // join the network, with nothing to say why.
    const QString hexKey(64, QLatin1Char('a'));
    QString notHex(64, QLatin1Char('a'));
    notHex[40] = QLatin1Char('z');
    REQUIRE(notHex.length() == 64);

    QVariantMap stored;
    stored["wifiSSID"] = "net";
    stored["wifiCountry"] = "GB";
    stored["wifiPassword"] = hexKey;
    const std::string withKey = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(stored)).toStdString();

    QVariantMap typed = stored;
    typed["wifiPassword"] = notHex;
    const std::string withPassphrase = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(typed)).toStdString();

    INFO("stored key:\n" << withKey << "\ntyped:\n" << withPassphrase);
    REQUIRE_THAT(withKey, ContainsSubstring("password_encrypted = true"));
    REQUIRE_THAT(withPassphrase, !ContainsSubstring("password_encrypted = true"));
}

TEST_CASE("rpi-preseed ssh section is gated on sshEnabled", "[preseed][ssh]") {
    QVariantMap settings;
    settings["sshAuthorizedKeys"] = "ssh-ed25519 AAAAKEY user@host";

    // sshEnabled defaults to false -> no [ssh] section, keys not leaked.
    std::string off = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings, QString(), false, false)).toStdString();
    REQUIRE(off.empty());

    // Enabled -> section present with the key in a multi-line array.
    std::string on = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings, QString(), false, true)).toStdString();
    REQUIRE_THAT(on, ContainsSubstring("[ssh]"));
    REQUIRE_THAT(on, ContainsSubstring("enabled = true"));
    REQUIRE_THAT(on, ContainsSubstring("password_authentication = false"));
    REQUIRE_THAT(on, ContainsSubstring("authorized_keys = ["));
    REQUIRE_THAT(on, ContainsSubstring("  \"ssh-ed25519 AAAAKEY user@host\","));
    REQUIRE_THAT(on, ContainsSubstring("]"));
}

TEST_CASE("rpi-preseed ssh multiple keys and password auth", "[preseed][ssh]") {
    QVariantMap settings;
    settings["sshPasswordAuth"] = true;
    settings["sshAuthorizedKeys"] = "ssh-rsa KEY1 a@b\nssh-ed25519 KEY2 c@d";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings, QString(), false, true)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("password_authentication = true"));
    REQUIRE_THAT(s, ContainsSubstring("\"ssh-rsa KEY1 a@b\","));
    REQUIRE_THAT(s, ContainsSubstring("\"ssh-ed25519 KEY2 c@d\","));
}

TEST_CASE("rpi-preseed wlan with pre-hashed PSK is marked encrypted", "[preseed][wifi]") {
    QVariantMap settings;
    settings["wifiSSID"] = "MyNet";
    // 64 hex chars: a raw PMK stored by the wizard as wifiPasswordCrypt.
    settings["wifiPasswordCrypt"] = QString(64, QChar('a'));
    settings["recommendedWifiCountry"] = "GB";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("[wlan]"));
    REQUIRE_THAT(s, ContainsSubstring("ssid = \"MyNet\""));
    REQUIRE_THAT(s, ContainsSubstring("password = \"" + std::string(64, 'a') + "\""));
    REQUIRE_THAT(s, ContainsSubstring("password_encrypted = true"));
    REQUIRE_THAT(s, ContainsSubstring("country = \"GB\""));
    REQUIRE_THAT(s, ContainsSubstring("hidden = false"));
    // key_mgmt is left implicit (rpi-preseed defaults to wpa-psk with a password).
    REQUIRE_THAT(s, !ContainsSubstring("key_mgmt"));
}

TEST_CASE("rpi-preseed open network emits no password", "[preseed][wifi]") {
    QVariantMap settings;
    settings["wifiSSID"] = "OpenNet";
    settings["wifiMode"] = "open";
    // A stale crypt value must not leak into an open-network config, which
    // rpi-preseed would reject.
    settings["wifiPasswordCrypt"] = "stalevalue";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("ssid = \"OpenNet\""));
    REQUIRE_THAT(s, !ContainsSubstring("password ="));
    REQUIRE_THAT(s, !ContainsSubstring("password_encrypted"));
}

TEST_CASE("rpi-preseed non-UTF-8 SSID is emitted as hex", "[preseed][wifi][exotic]") {
    QVariantMap settings;
    // Raw octets that are not valid UTF-8 (0xFF 0xFE ...), supplied base64-encoded
    // exactly as the wizard stores exotic SSIDs.
    QByteArray octets = QByteArray::fromHex("fffe4142");
    settings["wifiSsidOctetsBase64"] = octets.toBase64();

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    // Must NOT emit a corrupted ssid string; must emit the raw octets as hex.
    REQUIRE_THAT(s, ContainsSubstring("ssid_hex = \"fffe4142\""));
    REQUIRE_THAT(s, !ContainsSubstring("ssid ="));
}

TEST_CASE("rpi-preseed UTF-8 SSID uses ssid not ssid_hex", "[preseed][wifi]") {
    QVariantMap settings;
    settings["wifiSSID"] = "PlainNet";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("ssid = \"PlainNet\""));
    REQUIRE_THAT(s, !ContainsSubstring("ssid_hex"));
}

TEST_CASE("rpi-preseed hidden network flag", "[preseed][wifi]") {
    QVariantMap settings;
    settings["wifiSSID"] = "HiddenNet";
    settings["wifiHidden"] = true;

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("hidden = true"));
}

TEST_CASE("rpi-preseed locale section", "[preseed][locale]") {
    QVariantMap settings;
    settings["timezone"] = "Europe/London";
    settings["keyboard"] = "gb";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("[locale]"));
    REQUIRE_THAT(s, ContainsSubstring("keymap = \"gb\""));
    REQUIRE_THAT(s, ContainsSubstring("timezone = \"Europe/London\""));
}

TEST_CASE("rpi-preseed connect with token uses token mode", "[preseed][connect]") {
    QVariantMap settings;
    settings["piConnectEnabled"] = true;

    std::string withToken = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings, "deploy-token-123")).toStdString();
    REQUIRE_THAT(withToken, ContainsSubstring("[connect]"));
    REQUIRE_THAT(withToken, ContainsSubstring("enabled = true"));
    REQUIRE_THAT(withToken, ContainsSubstring("mode = \"token\""));
    REQUIRE_THAT(withToken, ContainsSubstring("token = \"deploy-token-123\""));

    std::string noToken = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();
    REQUIRE_THAT(noToken, ContainsSubstring("mode = \"device-identity\""));
    REQUIRE_THAT(noToken, !ContainsSubstring("token ="));
}

TEST_CASE("rpi-preseed interfaces map wizard values", "[preseed][interfaces]") {
    QVariantMap settings;
    settings["enableI2C"] = true;
    settings["enableSPI"] = false;  // false toggles are omitted, not forced off
    settings["enableUsbGadget"] = true;
    settings["enableSerial"] = "Console & Hardware";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("[interfaces]"));
    REQUIRE_THAT(s, ContainsSubstring("i2c = true"));
    REQUIRE_THAT(s, !ContainsSubstring("spi ="));
    REQUIRE_THAT(s, ContainsSubstring("usb_gadget = true"));
    REQUIRE_THAT(s, ContainsSubstring("serial = \"console_hardware\""));
}

TEST_CASE("rpi-preseed disabled serial is omitted", "[preseed][interfaces]") {
    QVariantMap settings;
    settings["enableSerial"] = "Disabled";

    QByteArray toml = CustomisationGenerator::generateRpiPreseedToml(settings);
    // No other content -> nothing at all.
    REQUIRE(toml.isEmpty());
}

TEST_CASE("rpi-preseed escapes basic strings for the parser", "[preseed][security]") {
    QVariantMap settings;
    // Backslash and double-quote are the only escapes rpi-preseed unescapes.
    settings["wifiSSID"] = "My\"Weird\\Net";

    std::string s = QString::fromUtf8(
        CustomisationGenerator::generateRpiPreseedToml(settings)).toStdString();

    REQUIRE_THAT(s, ContainsSubstring("ssid = \"My\\\"Weird\\\\Net\""));
}

TEST_CASE("rpi-preseed combined config orders sections", "[preseed]") {
    QVariantMap settings;
    settings["hostname"] = "cm5-jig";
    settings["sshUserName"] = "jig";
    settings["sshUserPassword"] = "$y$hash";
    settings["sshPasswordAuth"] = false;
    settings["wifiSSID"] = "Net";
    settings["wifiPasswordCrypt"] = QString(64, QChar('b'));
    settings["timezone"] = "Europe/London";

    QByteArray toml = CustomisationGenerator::generateRpiPreseedToml(
        settings, QString(), false, true);
    QString s = QString::fromUtf8(toml);

    // config_version must be first, sections follow in a stable order.
    REQUIRE(s.startsWith("config_version = \"1.0\""));
    REQUIRE(s.indexOf("[system]") < s.indexOf("[user]"));
    REQUIRE(s.indexOf("[user]") < s.indexOf("[ssh]"));
    REQUIRE(s.indexOf("[ssh]") < s.indexOf("[wlan]"));
    REQUIRE(s.indexOf("[wlan]") < s.indexOf("[locale]"));
}

// ===========================================================================
// Credential derivation (cryptPassword / pbkdf2) - moved here from the UI/
// ImageWriter layer. See issue #1627 for the CR/LF stripping rationale.
// ===========================================================================

TEST_CASE("cryptPassword selects algorithm by OS release date", "[customization][password][crypt]") {
    SECTION("OS released on/after 2023-01-01 uses yescrypt") {
        const QString hash = CustomisationGenerator::cryptPassword("hunter2", "2023-06-01");
        REQUIRE_THAT(hash.toStdString(), ContainsSubstring("$y$"));
    }

    SECTION("OS released before 2023-01-01 uses sha256crypt") {
        const QString hash = CustomisationGenerator::cryptPassword("hunter2", "2022-12-31");
        REQUIRE(hash.startsWith("$5$"));
    }

    SECTION("Missing release date defaults to sha256crypt") {
        const QString hash = CustomisationGenerator::cryptPassword("hunter2", QString());
        REQUIRE(hash.startsWith("$5$"));
    }
}

TEST_CASE("cryptPassword strips CR/LF before hashing", "[customization][password][crypt]") {
    // A pasted password may carry a trailing newline. PAM discards it at login,
    // so the stored hash must correspond to the password WITHOUT the newline,
    // otherwise sudo/login can never succeed (issue #1627).
    // Verify via crypt(3) semantics: re-hashing against the produced hash's
    // embedded salt reproduces it only for the stripped password.
    const QString hash = CustomisationGenerator::cryptPassword(QByteArray("hunter2\n"), "2022-01-01");
    REQUIRE(hash.startsWith("$5$"));

    const QByteArray hashBytes = hash.toUtf8();
    const QString rehashStripped = QString::fromUtf8(sha256_crypt("hunter2", hashBytes.constData()));
    const QString rehashRaw = QString::fromUtf8(sha256_crypt("hunter2\n", hashBytes.constData()));

    REQUIRE(rehashStripped == hash);   // stored hash matches the newline-free password
    REQUIRE(rehashRaw != hash);        // and not the raw pasted value
}

TEST_CASE("osUsesYescrypt picks the algorithm by release date", "[customization][password][crypt]") {
    REQUIRE(CustomisationGenerator::osUsesYescrypt("2023-01-01"));   // cutoff is inclusive
    REQUIRE(CustomisationGenerator::osUsesYescrypt("2024-06-01"));
    REQUIRE_FALSE(CustomisationGenerator::osUsesYescrypt("2022-12-31"));
    REQUIRE_FALSE(CustomisationGenerator::osUsesYescrypt(QString()));   // unknown -> conservative
    REQUIRE_FALSE(CustomisationGenerator::osUsesYescrypt("not-a-date"));
}

TEST_CASE("isYescryptHash detects the algorithm from the crypt prefix", "[customization][password][crypt]") {
    // The crypt string is self-describing, so the algorithm is never stored
    // separately. Only a yescrypt hash needs an OS-compatibility check; sha256crypt
    // is accepted everywhere.
    const QString yescrypt = CustomisationGenerator::cryptPassword("hunter2", "2024-03-01");
    const QString sha256 = CustomisationGenerator::cryptPassword("hunter2", "2021-01-01");
    REQUIRE(yescrypt.startsWith("$y$"));
    REQUIRE(sha256.startsWith("$5$"));

    REQUIRE(CustomisationGenerator::isYescryptHash(yescrypt));
    REQUIRE(CustomisationGenerator::isYescryptHash("$7$abc$def"));
    REQUIRE_FALSE(CustomisationGenerator::isYescryptHash(sha256));
    REQUIRE_FALSE(CustomisationGenerator::isYescryptHash("$6$salt$hash"));
    REQUIRE_FALSE(CustomisationGenerator::isYescryptHash(QString()));
}

TEST_CASE("Generator hashes plaintext account password at generation time", "[customization][password]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPasswordPlain"] = "s3cr3tpw";

    SECTION("yescrypt for a recent OS") {
        settings["osReleaseDate"] = "2024-03-01";
        const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));

        // The plaintext must never appear; only the derived hash is emitted.
        REQUIRE_THAT(script.toStdString(), !ContainsSubstring("s3cr3tpw"));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("/usr/lib/userconf-pi/userconf"));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("'$y$"));
    }

    SECTION("sha256crypt for an older OS") {
        settings["osReleaseDate"] = "2021-01-01";
        const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));

        REQUIRE_THAT(script.toStdString(), !ContainsSubstring("s3cr3tpw"));
        REQUIRE_THAT(script.toStdString(), ContainsSubstring("'$5$"));
    }
}

TEST_CASE("Generator prefers plaintext password over a stale crypted value", "[customization][password]") {
    // When both a freshly entered plaintext and an old crypted value are present,
    // the plaintext wins (the UI clears the crypted value, but be defensive).
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$5$stalesalt$stalehash";
    settings["sshUserPasswordPlain"] = "freshpw1";
    settings["osReleaseDate"] = "2021-01-01";

    const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));

    REQUIRE_THAT(script.toStdString(), !ContainsSubstring("$5$stalesalt$stalehash"));
    REQUIRE_THAT(script.toStdString(), !ContainsSubstring("freshpw1"));
    REQUIRE_THAT(script.toStdString(), ContainsSubstring("'$5$"));
}

TEST_CASE("Generator falls back to crypted password when no plaintext present", "[customization][password]") {
    // Reused-from-saved-settings path: an already-crypted value passes through.
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshUserPassword"] = "$5$savedsalt$savedhash";

    const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));
    REQUIRE_THAT(script.toStdString(), ContainsSubstring("$5$savedsalt$savedhash"));
}

TEST_CASE("Generator derives Wi-Fi PSK from plaintext passphrase", "[customization][wifi][password]") {
    QVariantMap settings;
    settings["wifiSSID"] = "TestNet";
    settings["wifiPassword"] = "supersecret"; // 11 chars -> passphrase

    const QString expectedPsk = CustomisationGenerator::pbkdf2(
        QByteArray("supersecret"), QByteArray("TestNet"));

    const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));
    REQUIRE_THAT(script.toStdString(), !ContainsSubstring("supersecret"));
    REQUIRE_THAT(script.toStdString(), ContainsSubstring("psk=" + expectedPsk.toStdString()));
}

TEST_CASE("Generator passes through a pre-derived Wi-Fi PSK unchanged", "[customization][wifi][password]") {
    QVariantMap settings;
    settings["wifiSSID"] = "TestNet";
    settings["wifiPasswordCrypt"] = "deadbeefcafef00d";

    const QString script = QString::fromUtf8(CustomisationGenerator::generateSystemdScript(settings));
    REQUIRE_THAT(script.toStdString(), ContainsSubstring("psk=deadbeefcafef00d"));
}


// ═══════════════════════════════════════════════════════════════════════════
// Does what we generate actually parse?
//
// Every case above checks the output with contains(): the right strings are
// present, the wrong ones absent. That cannot tell whether the document is
// syntactically valid, and a cloud-init user-data file that does not parse
// is not partially applied -- it is wholly ignored, so the user's hostname,
// user, Wi-Fi and SSH settings all silently fail together on first boot.
//
// The realistic way to break YAML or TOML is not a bug in the template but a
// value the user typed: a password containing a colon or a quote, an SSID
// with a hash, a hostname with a newline pasted in. So these run the output
// through real parsers, with values chosen to be awkward.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// The python3 that can parse `language`, or empty if none here can.
//
// Existing was not the same question as usable. macOS ships 3.9.6 at
// /usr/bin/python3, which has neither PyYAML nor tomllib -- tomllib arrived in
// 3.11 -- so five cases reported the generated documents as unparseable on a
// machine that simply had no parser. That is the failure these cases are for,
// which made it a convincing one. Ask the interpreter whether it can import
// the module, and try a python3 on PATH too: a Homebrew or pyenv one usually
// can, so the cases run rather than skip.
QString pythonThatParses(const char *language)
{
    const QString module = QString::fromLatin1(language) == QLatin1String("yaml")
                               ? QStringLiteral("yaml")
                               : QStringLiteral("tomllib");

    static QHash<QString, QString> cache;
    const auto cached = cache.constFind(module);
    if (cached != cache.cend())
        return *cached;

    QStringList candidates{QStringLiteral("/usr/bin/python3")};
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!onPath.isEmpty() && !candidates.contains(onPath))
        candidates << onPath;

    QString usable;
    for (const QString &python : std::as_const(candidates)) {
        if (!QFileInfo::exists(python))
            continue;
        QProcess proc;
        proc.start(python, {QStringLiteral("-c"), QStringLiteral("import ") + module});
        proc.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
        if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
            usable = python;
            break;
        }
    }
    cache.insert(module, usable);
    return usable;
}

// Parses `document` with python3 and returns true if the parser accepted it.
bool parsesAs(const QByteArray &document, const char *language, QString *error)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return false;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("document"));
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(document);
    }

    const QString script =
        QString::fromLatin1(language) == QLatin1String("yaml")
            ? QStringLiteral("import sys,yaml; yaml.safe_load(open(sys.argv[1],'rb').read())")
            : QStringLiteral("import sys,tomllib; tomllib.load(open(sys.argv[1],'rb'))");

    const QString python = pythonThatParses(language);
    if (python.isEmpty())
        return false;

    QProcess proc;
    proc.start(python, {QStringLiteral("-c"), script, path});
    proc.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    if (error)
        *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

// Settings with values chosen to be awkward for a document format.
QVariantMap awkwardSettings()
{
    QVariantMap s;
    s.insert(QStringLiteral("hostname"), QStringLiteral("pi-test"));
    s.insert(QStringLiteral("timezone"), QStringLiteral("Europe/London"));
    s.insert(QStringLiteral("keyboardLayout"), QStringLiteral("gb"));
    s.insert(QStringLiteral("sshUserName"), QStringLiteral("pi"));
    // A crypted password is the field most likely to carry $ : / and .
    s.insert(QStringLiteral("sshUserPassword"),
             QStringLiteral("$5$rounds=5000$abc:def$xyz/123.456"));
    s.insert(QStringLiteral("wifiSSID"), QStringLiteral("my: network #1 \"quoted\""));
    s.insert(QStringLiteral("wifiPassword"), QStringLiteral("p@ss: word #with 'quotes'"));
    s.insert(QStringLiteral("wifiCountry"), QStringLiteral("GB"));
    return s;
}

} // namespace

TEST_CASE("Generated cloud-init user-data is valid YAML", "[customisation][parse]")
{
    if (pythonThatParses("yaml").isEmpty())
        SKIP("no python3 with PyYAML here, so the output cannot be parsed");

    const QByteArray yaml =
        CustomisationGenerator::generateCloudInitUserData(awkwardSettings(), QString());
    REQUIRE_FALSE(yaml.isEmpty());

    QString error;
    INFO("document:\n" << QString::fromUtf8(yaml).left(1200).toStdString());
    INFO("parser said: " << error.toStdString());
    CHECK(parsesAs(yaml, "yaml", &error));
}

TEST_CASE("Generated cloud-init network config is valid YAML", "[customisation][parse]")
{
    if (pythonThatParses("yaml").isEmpty())
        SKIP("no python3 with PyYAML here, so the output cannot be parsed");

    const QByteArray yaml =
        CustomisationGenerator::generateCloudInitNetworkConfig(awkwardSettings(), false);
    if (yaml.isEmpty())
        SKIP("no network config generated for these settings");

    QString error;
    INFO("document:\n" << QString::fromUtf8(yaml).left(1200).toStdString());
    INFO("parser said: " << error.toStdString());
    CHECK(parsesAs(yaml, "yaml", &error));
}

TEST_CASE("Generated rpi-preseed is valid TOML", "[customisation][parse]")
{
    if (pythonThatParses("toml").isEmpty())
        SKIP("no python3 with tomllib here, so the output cannot be parsed");

    const QByteArray toml =
        CustomisationGenerator::generateRpiPreseedToml(awkwardSettings(), QString());
    REQUIRE_FALSE(toml.isEmpty());

    QString error;
    INFO("document:\n" << QString::fromUtf8(toml).left(1200).toStdString());
    INFO("parser said: " << error.toStdString());
    CHECK(parsesAs(toml, "toml", &error));
}

TEST_CASE("A value that looks like YAML cannot restructure the document",
          "[customisation][parse]")
{
    if (pythonThatParses("yaml").isEmpty())
        SKIP("no python3 with PyYAML here, so the output cannot be parsed");

    QVariantMap s = awkwardSettings();
    // Everything here is a value a user can type into the dialog. If any of
    // it reaches the document unescaped it stops being a value and becomes
    // structure -- which is how a Wi-Fi password ends up disabling SSH, or
    // the whole file stops parsing and no customisation applies at all.
    s.insert(QStringLiteral("wifiSSID"),
             QStringLiteral("net\nssh_pwauth: true\nfoo: bar"));
    s.insert(QStringLiteral("wifiPassword"),
             QStringLiteral("pw\"\n- injected\n  nested: yes"));
    s.insert(QStringLiteral("hostname"), QStringLiteral("host\nchpasswd:\n  expire: false"));

    const QByteArray yaml =
        CustomisationGenerator::generateCloudInitUserData(s, QString());
    REQUIRE_FALSE(yaml.isEmpty());

    QString error;
    const bool parsed = parsesAs(yaml, "yaml", &error);
    INFO("document:\n" << QString::fromUtf8(yaml).left(1500).toStdString());
    INFO("parser said: " << error.toStdString());
    // Either the generator rejects these values or it quotes them. What it
    // must not do is emit something that parses into a different shape.
    CHECK(parsed);
}

TEST_CASE("A value that looks like TOML cannot restructure the document",
          "[customisation][parse]")
{
    if (pythonThatParses("toml").isEmpty())
        SKIP("no python3 with tomllib here, so the output cannot be parsed");

    QVariantMap s = awkwardSettings();
    s.insert(QStringLiteral("wifiSSID"),
             QStringLiteral("net\"\n[injected]\nkey = \"value"));
    s.insert(QStringLiteral("sshUserName"),
             QStringLiteral("user\"\nadmin = true"));

    const QByteArray toml = CustomisationGenerator::generateRpiPreseedToml(s, QString());
    REQUIRE_FALSE(toml.isEmpty());

    QString error;
    const bool parsed = parsesAs(toml, "toml", &error);
    INFO("document:\n" << QString::fromUtf8(toml).left(1500).toStdString());
    INFO("parser said: " << error.toStdString());
    CHECK(parsed);
}

// ---------------------------------------------------------------------------
// An SSID that is not UTF-8 at all
// ---------------------------------------------------------------------------
//
// An SSID is arbitrary octets. Access points are perfectly entitled to name
// themselves in Latin-1, in Shift-JIS, or in nothing in particular, and a
// user who lives on such a network has to be able to join it.
//
// The branches that notice were uncovered: the existing cases reach the hex
// path through a quote or a backslash, which are caught by the character
// scan rather than by the UTF-8 check. Bytes that are simply not valid UTF-8
// take the other route.
//
// Written out as if they were a quoted string, wpa_supplicant gets a name
// that is not the one the access point broadcasts, and the board silently
// never joins. Written into cloud-init's YAML unescaped, the file does not
// parse and every setting in it is dropped, not just the network.

namespace {

// A lone continuation byte, then a truncated two-byte sequence: neither is
// decodable, and both are legal in an SSID.
QByteArray notUtf8Ssid()
{
    QByteArray octets("Caf");
    octets.append(char(0xE9));      // Latin-1 e-acute, invalid on its own
    octets.append("-net");
    octets.append(char(0xC3));      // starts a two-byte sequence and stops
    return octets;
}

} // namespace

TEST_CASE("An SSID that is not UTF-8 goes in as hex", "[customization][wifi]")
{
    const QByteArray octets = notUtf8Ssid();
    const QByteArray script = CustomisationGenerator::generateSystemdScript(
        exoticWifiSettingsFromOctets(octets));
    const std::string text = QString::fromUtf8(script).toStdString();

    REQUIRE_THAT(text, ContainsSubstring("ssid=hex:" + octets.toHex().toStdString()));
    // And not as a quoted string, which is what it would be if the UTF-8
    // check reported these bytes as fine.
    REQUIRE_THAT(text, !ContainsSubstring("ssid=\"Caf"));
}

TEST_CASE("A valid UTF-8 SSID with no awkward characters stays quoted",
          "[customization][wifi]")
{
    // The counterpart: hex for everything would work but is unreadable in a
    // file users are told they can edit, so the plain case has to stay plain.
    const QByteArray octets = QString::fromUtf8("Café-📶").toUtf8();
    const QByteArray script = CustomisationGenerator::generateSystemdScript(
        exoticWifiSettingsFromOctets(octets));
    const std::string text = QString::fromUtf8(script).toStdString();

    REQUIRE_THAT(text, ContainsSubstring("ssid=\"Caf"));
    REQUIRE_THAT(text, !ContainsSubstring("ssid=hex:"));
}

TEST_CASE("An empty SSID is treated as valid rather than hex-encoded",
          "[customization][wifi]")
{
    // Nothing to decode is not a decoding failure. A hidden network with no
    // name is a real configuration.
    const QByteArray script = CustomisationGenerator::generateSystemdScript(
        exoticWifiSettingsFromOctets(QByteArray(), QStringLiteral("hash"), true));
    const std::string text = QString::fromUtf8(script).toStdString();

    REQUIRE_THAT(text, !ContainsSubstring("ssid=hex:"));
}

TEST_CASE("Bytes that are not UTF-8 are escaped for cloud-init, not passed through",
          "[customization][wifi]")
{
    // YAML has no way to carry a raw undecodable byte. Passed through, the
    // file does not parse and cloud-init drops every setting in it -- the
    // user account and the SSH keys along with the network.
    const QByteArray escaped =
        CustomisationGenerator::yamlEscapeSsidOctets(notUtf8Ssid());

    CHECK(escaped.contains("\\xe9"));
    CHECK(escaped.contains("\\xc3"));
    for (const char c : escaped) {
        INFO("escaped output: " << escaped.toStdString());
        CHECK(static_cast<unsigned char>(c) < 0x80);
    }
}

TEST_CASE("A control character in an SSID is escaped for cloud-init",
          "[customization][wifi]")
{
    QByteArray octets("tab");
    octets.append(char(0x09));
    octets.append("newline");
    octets.append(char(0x0A));
    octets.append("bell");
    octets.append(char(0x07));

    const QByteArray escaped =
        CustomisationGenerator::yamlEscapeSsidOctets(octets);

    CHECK(escaped.contains("\\t"));
    CHECK(escaped.contains("\\n"));
    CHECK(escaped.contains("\\x07"));
    CHECK(!escaped.contains(char(0x0A)));
}

TEST_CASE("A null byte in an SSID is escaped rather than truncating the name",
          "[customization][wifi]")
{
    // A QByteArray carries it happily; anything that treats the buffer as a
    // C string stops there and configures half a network name.
    QByteArray octets("before");
    octets.append(char(0));
    octets.append("after");

    const QByteArray escaped =
        CustomisationGenerator::yamlEscapeSsidOctets(octets);

    CHECK(escaped.contains("\\0"));
    CHECK(escaped.contains("after"));
}
