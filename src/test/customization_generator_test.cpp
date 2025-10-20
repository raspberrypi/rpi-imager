/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "customization_generator.h"
#include <QVariantMap>
#include <QString>
#include <QByteArray>

using namespace rpi_imager;
using Catch::Matchers::ContainsSubstring;

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
    settings["wifiSSID"] = "TestNetwork";
    settings["wifiPasswordCrypt"] = "hashed_password_here";
    settings["wifiCountry"] = "GB";
    settings["wifiHidden"] = true;
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_wlan"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("country=GB"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=\"TestNetwork\""));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("scan_ssid=1"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("psk=hashed_password_here"));
}

TEST_CASE("CustomisationGenerator WiFi configuration with empty PSK", "[customization]") {
    QVariantMap settings;
    settings["wifiSSID"] = "OpenNetwork";
    settings["wifiPasswordCrypt"] = "";  // Empty PSK for open network
    settings["wifiCountry"] = "US";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Should still include psk= line even when empty (matching old Imager behavior)
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=\"OpenNetwork\""));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("\tpsk="));
}

TEST_CASE("CustomisationGenerator WiFi country only (no SSID)", "[customization]") {
    QVariantMap settings;
    // Set country code GB without SSID - tests regulatory domain configuration
    settings["wifiCountry"] = "GB";
    
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
    
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("com.raspberrypi.connect"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("deploy.key"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("test-token-12345"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("rpi-connect-signin.service"));
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
    settings["wifiSSID"] = "Test Network (5GHz)";
    settings["wifiPasswordCrypt"] = "fakehash";
    settings["wifiCountry"] = "US";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // SSID with parentheses and spaces should work fine
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=\"Test Network (5GHz)\""));
}

TEST_CASE("CustomisationGenerator handles quotes in WiFi SSID", "[customization][negative]") {
    QVariantMap settings;
    settings["wifiSSID"] = "My \"Quoted\" Network";
    settings["wifiPasswordCrypt"] = "fakehash";
    settings["wifiCountry"] = "US";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Quotes should be escaped in wpa_supplicant.conf
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=\"My \\\"Quoted\\\" Network\""));
    // Should still be properly shell-quoted for imager_custom
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_wlan"));
}

TEST_CASE("CustomisationGenerator handles backslashes in WiFi SSID", "[customization][negative]") {
    QVariantMap settings;
    settings["wifiSSID"] = "Network\\With\\Backslashes";
    settings["wifiPasswordCrypt"] = "fakehash";
    settings["wifiCountry"] = "US";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // Backslashes should be escaped in wpa_supplicant.conf
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("ssid=\"Network\\\\With\\\\Backslashes\""));
}

TEST_CASE("CustomisationGenerator handles non-ASCII UTF-8 WiFi SSID", "[customization][negative]") {
    QVariantMap settings;
    settings["wifiSSID"] = "Café ☕ 日本語";
    settings["wifiPasswordCrypt"] = "fakehash";  // Pre-computed PSK (passwords are ASCII-only per WPA2 spec)
    settings["wifiCountry"] = "FR";
    
    QByteArray script = CustomisationGenerator::generateSystemdScript(settings);
    QString scriptStr = QString::fromUtf8(script);
    
    // NOTE: SSIDs support full UTF-8 per WiFi spec. Passwords are ASCII-only (8-63 chars) or
    // pre-computed 64-char hex PSK per WPA2/WPA3 spec. The UI enforces this correctly.
    // This test validates the generator handles UTF-8 SSIDs robustly for:
    // - Edge cases that bypass UI validation
    // - Future WPA standards that may allow UTF-8 passphrases
    // - Programmatic/CLI usage
    
    // UTF-8 characters should pass through correctly in both paths
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("wpa_supplicant.conf"));
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("Café ☕ 日本語"));
    // Also verify shell-quoted path works
    REQUIRE_THAT(scriptStr.toStdString(), ContainsSubstring("set_wlan"));
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
    REQUIRE_FALSE(scriptStr.contains("com.raspberrypi.connect"));
    REQUIRE_FALSE(scriptStr.contains("deploy.key"));
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
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hostname: testpi"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("manage_etc_hosts: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("packages:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- avahi-daemon"));
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
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("enable_ssh: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("users:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- name: testuser"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("groups: users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("shell: /bin/sh"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("lock_passwd: false"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("passwd: $5$fakesalt$fakehash123"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with SSH keys", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["sshUserName"] = "testuser";
    settings["sshAuthorizedKeys"] = "ssh-rsa AAAAB3...key1\nssh-rsa AAAAB3...key2";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "testuser");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("enable_ssh: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_authorized_keys:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- ssh-rsa AAAAB3...key1"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- ssh-rsa AAAAB3...key2"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("lock_passwd: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("sudo: ALL=(ALL) NOPASSWD:ALL"));
}

TEST_CASE("CustomisationGenerator generates cloud-init user-data with password auth", "[cloudinit][userdata]") {
    QVariantMap settings;
    settings["sshPasswordAuth"] = true;
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, QString(), false, true, "testuser");
    QString yaml = QString::fromUtf8(userdata);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("ssh_pwauth: true"));
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
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("write_files:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("- path: /home/testuser/com.raspberrypi.connect/auth.key"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("permissions: '0600'"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("owner: testuser:testuser"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("content: |"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("test-token-abcd-1234"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("runcmd:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("install -o testuser -m 700 -d /home/testuser/com.raspberrypi.connect"));
}

TEST_CASE("CustomisationGenerator generates cloud-init network-config with WiFi", "[cloudinit][network]") {
    QVariantMap settings;
    settings["wifiSSID"] = "TestNetwork";
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    settings["wifiCountry"] = "DE";
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("network:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("version: 2"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("wifis:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("wlan0:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("dhcp4: true"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("regulatory-domain: \"DE\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("access-points:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"TestNetwork\":"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("password: \"fakecryptedhash123\""));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("optional: true"));
}

TEST_CASE("CustomisationGenerator generates cloud-init network-config with hidden WiFi", "[cloudinit][network]") {
    QVariantMap settings;
    settings["wifiSSID"] = "HiddenNetwork";
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    settings["wifiHidden"] = true;
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("\"HiddenNetwork\":"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("hidden: true"));
}

TEST_CASE("CustomisationGenerator cloud-init WiFi country only (no SSID)", "[cloudinit][network]") {
    QVariantMap settings;
    // Set country code FR without SSID - tests regulatory domain configuration
    settings["wifiCountry"] = "FR";
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings, "", false, false, "pi");
    QString yaml = QString::fromUtf8(userdata);
    
    // Should include runcmd to unblock WiFi
    // This prevents "Wi-Fi is currently blocked by rfkill" message
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("runcmd:"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("rfkill, unblock, wifi"));
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("/var/lib/systemd/rfkill/*:wlan"));
    
    // Network config should include the FR regulatory domain even without SSID
    // This verifies the country code makes it into the network configuration
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString netcfgYaml = QString::fromUtf8(netcfg);
    REQUIRE_FALSE(netcfg.isEmpty());
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("regulatory-domain: \"FR\""));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("wlan0:"));
    REQUIRE_THAT(netcfgYaml.toStdString(), ContainsSubstring("optional: true"));
    // Should NOT have access-points section (no SSID configured)
    REQUIRE_THAT(netcfgYaml.toStdString(), !ContainsSubstring("access-points:"));
}

TEST_CASE("CustomisationGenerator generates cloud-init network-config with special characters in SSID", "[cloudinit][network][negative]") {
    QVariantMap settings;
    settings["wifiSSID"] = "Test \"Network\" (5GHz)";
    settings["wifiPasswordCrypt"] = "fakecryptedhash123";
    
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings, false);
    QString yaml = QString::fromUtf8(netcfg);
    
    // Quotes should be escaped
    REQUIRE_THAT(yaml.toStdString(), ContainsSubstring("Test \\\"Network\\\" (5GHz)"));
}

TEST_CASE("CustomisationGenerator handles empty cloud-init settings gracefully", "[cloudinit][negative]") {
    QVariantMap settings;  // Empty settings
    
    QByteArray userdata = CustomisationGenerator::generateCloudInitUserData(settings);
    QByteArray netcfg = CustomisationGenerator::generateCloudInitNetworkConfig(settings);
    
    // Should return empty or minimal YAML
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
    REQUIRE_FALSE(yaml.contains("com.raspberrypi.connect"));
}

