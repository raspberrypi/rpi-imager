/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "customization_generator.h"
#include <QPasswordDigestor>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStringList>

namespace rpi_imager {

QString CustomisationGenerator::shellQuote(const QString& value) {
    QString t = value;
    t.replace("'", "'\"'\"'");
    return QString("'") + t + QString("'");
}

QString CustomisationGenerator::pbkdf2(const QByteArray& password, const QByteArray& ssid) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha1, password, ssid, 4096, 32).toHex();
}

QString CustomisationGenerator::yamlEscapeString(const QString& value) {
    // Escape a string for use in YAML double-quoted strings.
    // Per IEEE 802.11, SSIDs can be 0-32 octets containing ANY byte value,
    // including null, control characters, and non-printable bytes.
    // YAML double-quoted strings use backslash escapes for special characters.
    QString result;
    result.reserve(value.size() * 2);  // Worst case: all chars need escaping
    
    for (const QChar& ch : value) {
        ushort code = ch.unicode();
        
        if (code == '\\') {
            result += QStringLiteral("\\\\");
        } else if (code == '"') {
            result += QStringLiteral("\\\"");
        } else if (code == '\n') {
            result += QStringLiteral("\\n");
        } else if (code == '\r') {
            result += QStringLiteral("\\r");
        } else if (code == '\t') {
            result += QStringLiteral("\\t");
        } else if (code == '\b') {
            result += QStringLiteral("\\b");
        } else if (code == '\f') {
            result += QStringLiteral("\\f");
        } else if (code == 0) {
            result += QStringLiteral("\\0");
        } else if (code < 0x20 || code == 0x7F) {
            // Other control characters (0x01-0x07, 0x0B, 0x0E-0x1F, 0x7F) - use hex escape
            result += QString(QStringLiteral("\\x%1")).arg(code, 2, 16, QChar('0'));
        } else {
            // Safe to include as-is (printable ASCII and Unicode above 0x7F)
            result += ch;
        }
    }
    
    return result;
}

QByteArray CustomisationGenerator::generateSystemdScript(const QVariantMap& s, const QString& piConnectToken) {
    QByteArray script;
    auto line = [](const QString& l, QByteArray& out) { out += l.toUtf8(); out += '\n'; };

    const QString hostname = s.value("hostname").toString().trimmed();
    const QString timezone = s.value("timezone").toString().trimmed();
    const bool sshEnabled = s.value("sshEnabled").toBool();
    const bool sshPasswordAuth = s.value("sshPasswordAuth").toBool();
    const QString userName = s.value("sshUserName").toString().trimmed();
    const QString userPass = s.value("sshUserPassword").toString(); // crypted if present
    const QString sshPublicKey = s.value("sshPublicKey").toString().trimmed();
    const QString sshAuthorizedKeys = s.value("sshAuthorizedKeys").toString().trimmed();
    const QString ssid = s.value("wifiSSID").toString();
    const QString cryptedPskFromSettings = s.value("wifiPasswordCrypt").toString();
    bool hidden = s.value("wifiHidden").toBool();
    if (!hidden)
        hidden = s.value("wifiSSIDHidden").toBool();
    const QString wifiCountry = s.value("recommendedWifiCountry").toString().trimmed();
    
    // Prefer persisted crypted PSK; fallback to deriving from legacy plaintext setting
    QString cryptedPsk = cryptedPskFromSettings;
    if (cryptedPsk.isEmpty()) {
        const QString legacyPwd = s.value("wifiPassword").toString();
        const bool isPassphrase = (legacyPwd.length() >= 8 && legacyPwd.length() < 64);
        cryptedPsk = isPassphrase ? pbkdf2(legacyPwd.toUtf8(), ssid.toUtf8()) : legacyPwd;
    }
    
    // Prepare SSH key arguments for imager_custom
    QStringList keyList;
    if (!sshAuthorizedKeys.isEmpty()) {
        const QStringList lines = sshAuthorizedKeys.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
        for (const QString& k : lines) keyList.append(k.trimmed());
    } else if (!sshPublicKey.isEmpty()) {
        // Split sshPublicKey by newlines to handle .pub files with multiple keys
        const QStringList lines = sshPublicKey.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
        for (const QString& k : lines) keyList.append(k.trimmed());
    }
    QString pubkeyArgs;
    for (const QString& k : keyList) {
        pubkeyArgs += " ";
        pubkeyArgs += shellQuote(k);
    }

    const QString effectiveUser = userName.isEmpty() ? QStringLiteral("pi") : userName;
    const QString groups = QStringLiteral("users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo");

    line(QStringLiteral("#!/bin/sh"), script);
    line(QStringLiteral(""), script);
    line(QStringLiteral("set +e"), script);
    line(QStringLiteral(""), script);

    if (!hostname.isEmpty()) {
        line(QStringLiteral("CURRENT_HOSTNAME=$(cat /etc/hostname | tr -d \" \\t\\n\\r\")"), script);
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_hostname ") + hostname, script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("   echo ") + hostname + QStringLiteral(" >/etc/hostname"), script);
        line(QStringLiteral("   sed -i \"s/127.0.1.1.*$CURRENT_HOSTNAME/127.0.1.1\\t") + hostname + QStringLiteral("/g\" /etc/hosts"), script);
        line(QStringLiteral("fi"), script);
    }

    // Determine first user (uid 1000) and home, for parity with legacy behavior
    line(QStringLiteral("FIRSTUSER=$(getent passwd 1000 | cut -d: -f1)"), script);
    line(QStringLiteral("FIRSTUSERHOME=$(getent passwd 1000 | cut -d: -f6)"), script);

    if (!keyList.isEmpty()) {
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh -k") + pubkeyArgs, script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("   install -o \"$FIRSTUSER\" -m 700 -d \"$FIRSTUSERHOME/.ssh\""), script);
        // Fallback: write keys with heredoc (simpler/more robust than process substitution in this context)
        QString allKeys = keyList.join("\n");
        line(QStringLiteral("cat > \"$FIRSTUSERHOME/.ssh/authorized_keys\" <<'EOF'"), script);
        line(allKeys, script);
        line(QStringLiteral("EOF"), script);
        line(QStringLiteral("   chown \"$FIRSTUSER:$FIRSTUSER\" \"$FIRSTUSERHOME/.ssh/authorized_keys\""), script);
        line(QStringLiteral("   chmod 600 \"$FIRSTUSERHOME/.ssh/authorized_keys\""), script);
        line(QStringLiteral("   echo 'PasswordAuthentication no' >>/etc/ssh/sshd_config"), script);
        line(QStringLiteral("   systemctl enable ssh"), script);
        line(QStringLiteral("fi"), script);
    }

    if (sshEnabled && sshPasswordAuth && keyList.isEmpty()) {
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh"), script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("   systemctl enable ssh"), script);
        line(QStringLiteral("fi"), script);
    }

    // User and password setup with userconf integration
    // Also run when SSH public keys are configured (even without password) to ensure
    // the system is marked as configured and the initial setup wizard is skipped
    if (!userName.isEmpty() || !userPass.isEmpty() || !keyList.isEmpty()) {
        line(QStringLiteral("if [ -f /usr/lib/userconf-pi/userconf ]; then"), script);
        line(QStringLiteral("   /usr/lib/userconf-pi/userconf ") + shellQuote(effectiveUser) + QStringLiteral(" ") + shellQuote(userPass), script);
        line(QStringLiteral("else"), script);
        if (!userPass.isEmpty()) {
            line(QStringLiteral("   echo \"$FIRSTUSER:") + userPass + QStringLiteral("\" | chpasswd -e"), script);
        }
        line(QStringLiteral("   if [ \"$FIRSTUSER\" != \"") + effectiveUser + QStringLiteral("\" ]; then"), script);
        line(QStringLiteral("      usermod -l \"") + effectiveUser + QStringLiteral("\" \"$FIRSTUSER\""), script);
        line(QStringLiteral("      usermod -m -d \"/home/") + effectiveUser + QStringLiteral("\" \"") + effectiveUser + QStringLiteral("\""), script);
        line(QStringLiteral("      groupmod -n \"") + effectiveUser + QStringLiteral("\" \"$FIRSTUSER\""), script);
        line(QStringLiteral("      if grep -q \"^autologin-user=\" /etc/lightdm/lightdm.conf ; then"), script);
        line(QStringLiteral("         sed /etc/lightdm/lightdm.conf -i -e \"s/^autologin-user=.*/autologin-user=") + effectiveUser + QStringLiteral("/\""), script);
        line(QStringLiteral("      fi"), script);
        line(QStringLiteral("      if [ -f /etc/systemd/system/getty@tty1.service.d/autologin.conf ]; then"), script);
        line(QStringLiteral("         sed /etc/systemd/system/getty@tty1.service.d/autologin.conf -i -e \"s/$FIRSTUSER/") + effectiveUser + QStringLiteral("/\""), script);
        line(QStringLiteral("      fi"), script);
        line(QStringLiteral("      if [ -f /etc/sudoers.d/010_pi-nopasswd ]; then"), script);
        line(QStringLiteral("         sed -i \"s/^$FIRSTUSER /") + effectiveUser + QStringLiteral(" /\" /etc/sudoers.d/010_pi-nopasswd"), script);
        line(QStringLiteral("      fi"), script);
        line(QStringLiteral("   fi"), script);
        line(QStringLiteral("fi"), script);
    }

    if (!ssid.isEmpty()) {
        // Prefer imager_custom set_wlan; fallback to manual wpa_supplicant
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        QString wlanCmd = QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_wlan ");
        if (hidden) wlanCmd += QStringLiteral(" -h ");
        wlanCmd += shellQuote(ssid) + QStringLiteral(" ") + shellQuote(cryptedPsk) + QStringLiteral(" ") + shellQuote(wifiCountry);
        line(wlanCmd, script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("cat >/etc/wpa_supplicant/wpa_supplicant.conf <<'WPAEOF'"), script);
        if (!wifiCountry.isEmpty()) line(QStringLiteral("country=") + wifiCountry, script);
        line(QStringLiteral("ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev"), script);
        line(QStringLiteral("ap_scan=1"), script);
        line(QStringLiteral(""), script);
        line(QStringLiteral("update_config=1"), script);
        line(QStringLiteral("network={"), script);
        if (hidden) line(QStringLiteral("\tscan_ssid=1"), script);
        // Escape quotes and backslashes for wpa_supplicant.conf quoted strings
        QString escapedSsid = ssid;
        escapedSsid.replace("\\", "\\\\");  // Backslash must be escaped first
        escapedSsid.replace("\"", "\\\"");  // Then escape quotes
        line(QStringLiteral("\tssid=\"") + escapedSsid + QStringLiteral("\""), script);
        // WPA2/WPA3 transition mode: allow connection to both WPA2-PSK and WPA3-SAE networks
        line(QStringLiteral("\tkey_mgmt=WPA-PSK SAE"), script);
        line(QStringLiteral("\tpsk=") + cryptedPsk, script);
        // ieee80211w=1 enables optional Protected Management Frames (required for WPA3, optional for WPA2)
        line(QStringLiteral("\tieee80211w=1"), script);
        line(QStringLiteral("}"), script);
        line(QStringLiteral("WPAEOF"), script);
        line(QStringLiteral("   chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf"), script);
        line(QStringLiteral("   rfkill unblock wifi"), script);
        line(QStringLiteral("   for filename in /var/lib/systemd/rfkill/*:wlan ; do"), script);
        line(QStringLiteral("       echo 0 > $filename"), script);
        line(QStringLiteral("   done"), script);
        line(QStringLiteral("fi"), script);
    } else if (!wifiCountry.isEmpty()) {
        // When country is set but no SSID, still need to unblock Wi-Fi
        // This prevents "Wi-Fi is currently blocked by rfkill" message on boot
        line(QStringLiteral("rfkill unblock wifi"), script);
        line(QStringLiteral("for filename in /var/lib/systemd/rfkill/*:wlan ; do"), script);
        line(QStringLiteral("  echo 0 > $filename"), script);
        line(QStringLiteral("done"), script);
    }

    // Raspberry Pi Connect token provisioning (store in target user's home)
    const bool piConnectEnabled = s.value("piConnectEnabled").toBool();
    QString piConnectTokenTrimmed = piConnectToken.trimmed();
    if (piConnectEnabled && !piConnectTokenTrimmed.isEmpty()) {
        const QString configDir = QStringLiteral("$TARGET_HOME/") + PI_CONNECT_CONFIG_PATH;
        const QString deployKeyPath = configDir + QStringLiteral("/") + PI_CONNECT_DEPLOY_KEY_FILENAME;
        
        line(QStringLiteral("TARGET_USER=\"") + effectiveUser + QStringLiteral("\""), script);
        line(QStringLiteral("TARGET_HOME=$(getent passwd \"$TARGET_USER\" | cut -d: -f6)"), script);
        line(QStringLiteral("if [ -z \"$TARGET_HOME\" ] || [ ! -d \"$TARGET_HOME\" ]; then TARGET_HOME=\"/home/") + effectiveUser + QStringLiteral("\"; fi"), script);
        line(QStringLiteral("install -o \"$TARGET_USER\" -m 700 -d \"") + configDir + QStringLiteral("\""), script);
        line(QStringLiteral("cat > \"") + deployKeyPath + QStringLiteral("\" <<'EOF'"), script);
        line(piConnectTokenTrimmed, script);
        line(QStringLiteral("EOF"), script);
        line(QStringLiteral("chown \"$TARGET_USER:$TARGET_USER\" \"") + deployKeyPath + QStringLiteral("\""), script);
        line(QStringLiteral("chmod 600 \"") + deployKeyPath + QStringLiteral("\""), script);

        // Enable systemd user services for Raspberry Pi Connect
        line(QStringLiteral("# Enable Raspberry Pi Connect systemd units"), script);
        line(QStringLiteral("SYSTEMD_USER_BASE=\"$TARGET_HOME/.config/systemd/user\""), script);
        line(QStringLiteral("install -o \"$TARGET_USER\" -m 700 -d \"$SYSTEMD_USER_BASE/default.target.wants\" \"$SYSTEMD_USER_BASE/paths.target.wants\""), script);
        
        // Enable rpi-connect.service in default.target.wants
        line(QStringLiteral("UNIT_SRC=\"/usr/lib/systemd/user/rpi-connect.service\"; [ -f \"$UNIT_SRC\" ] || UNIT_SRC=\"/lib/systemd/user/rpi-connect.service\""), script);
        line(QStringLiteral("ln -sf \"$UNIT_SRC\" \"$SYSTEMD_USER_BASE/default.target.wants/rpi-connect.service\""), script);
        
        // Enable rpi-connect-signin.path in paths.target.wants (path units need to be independently enabled)
        line(QStringLiteral("UNIT_SRC=\"/usr/lib/systemd/user/rpi-connect-signin.path\"; [ -f \"$UNIT_SRC\" ] || UNIT_SRC=\"/lib/systemd/user/rpi-connect-signin.path\""), script);
        line(QStringLiteral("ln -sf \"$UNIT_SRC\" \"$SYSTEMD_USER_BASE/paths.target.wants/rpi-connect-signin.path\""), script);
        
        // Enable rpi-connect-wayvnc.service in default.target.wants
        line(QStringLiteral("UNIT_SRC=\"/usr/lib/systemd/user/rpi-connect-wayvnc.service\"; [ -f \"$UNIT_SRC\" ] || UNIT_SRC=\"/lib/systemd/user/rpi-connect-wayvnc.service\""), script);
        line(QStringLiteral("ln -sf \"$UNIT_SRC\" \"$SYSTEMD_USER_BASE/default.target.wants/rpi-connect-wayvnc.service\""), script);
        
        line(QStringLiteral("chown -R \"$TARGET_USER:$TARGET_USER\" \"$TARGET_HOME/.config/systemd\" || true"), script);
        
        // Set up systemd linger to enable auto-start via logind
        line(QStringLiteral("install -d -m 0755 /var/lib/systemd/linger"), script);
        line(QStringLiteral("install -m 0644 /dev/null \"/var/lib/systemd/linger/$TARGET_USER\""), script);
        
        // Start the user services now (linger ensures user manager is running)
        // Use loginctl to ensure user manager is active, then reload and start services
        line(QStringLiteral("loginctl enable-linger \"$TARGET_USER\" 2>/dev/null || true"), script);
        line(QStringLiteral("# Give user manager time to start if it wasn't already running"), script);
        line(QStringLiteral("sleep 2"), script);
        line(QStringLiteral("# Reload user systemd manager and start services"), script);
        line(QStringLiteral("systemctl --quiet --user --machine ${TARGET_USER}@.host daemon-reload"), script);
        line(QStringLiteral("systemctl --quiet --user --machine ${TARGET_USER}@.host start rpi-connect.service rpi-connect-signin.path rpi-connect-wayvnc.service"), script);
    }

    // Locale configuration (keyboard and timezone) - match legacy script structure
    const QString keyboardLayout = s.value("keyboard").toString().trimmed();
    if (!keyboardLayout.isEmpty() || !timezone.isEmpty()) {
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        if (!keyboardLayout.isEmpty()) {
            line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_keymap ") + shellQuote(keyboardLayout), script);
        }
        if (!timezone.isEmpty()) {
            line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_timezone ") + shellQuote(timezone), script);
        }
        line(QStringLiteral("else"), script);
        if (!timezone.isEmpty()) {
            line(QStringLiteral("   rm -f /etc/localtime"), script);
            line(QStringLiteral("   echo \"") + timezone + QStringLiteral("\" >/etc/timezone"), script);
            line(QStringLiteral("   dpkg-reconfigure -f noninteractive tzdata"), script);
        }
        if (!keyboardLayout.isEmpty()) {
            line(QStringLiteral("cat >/etc/default/keyboard <<'KBEOF'"), script);
            line(QStringLiteral("XKBMODEL=\"pc105\""), script);
            line(QStringLiteral("XKBLAYOUT=\"") + keyboardLayout + QStringLiteral("\""), script);
            line(QStringLiteral("XKBVARIANT=\"\""), script);
            line(QStringLiteral("XKBOPTIONS=\"\""), script);
            line(QStringLiteral(""), script);
            line(QStringLiteral("KBEOF"), script);
            line(QStringLiteral("   dpkg-reconfigure -f noninteractive keyboard-configuration"), script);
        }
        line(QStringLiteral("fi"), script);
    }

    // Final cleanup to mimic legacy behavior
    line(QStringLiteral("rm -f /boot/firstrun.sh"), script);
    line(QStringLiteral("sed -i 's| systemd.run.*||g' /boot/cmdline.txt"), script);
    line(QStringLiteral("exit 0"), script);

    return script;
}

QByteArray CustomisationGenerator::generateCloudInitUserData(const QVariantMap& settings,
                                                             const QString& piConnectToken,
                                                             bool hasCcRpi,
                                                             bool sshEnabled,
                                                             const QString& currentUser)
{
    QByteArray cloud;
    
    auto push = [](const QString& line, QByteArray& out) {
        if (!line.isEmpty()) {
            out += line.toUtf8();
            out += '\n';
        }
    };
    
    // Don't let cloud-init manage DNS - let dhcpcd/NetworkManager handle it
    push(QStringLiteral("manage_resolv_conf: false"), cloud);
    push(QString(), cloud);
    
    const QString hostname = settings.value("hostname").toString().trimmed();
    if (!hostname.isEmpty()) {
        push(QStringLiteral("hostname: ") + hostname, cloud);
        push(QStringLiteral("manage_etc_hosts: true"), cloud);
        // Note: We don't set preserve_hostname: true here because it would prevent
        // cloud-init from setting the hostname on first boot. Cloud-init's per-instance
        // behavior (via unique instance-id) ensures hostname is only set once.
        // Parity with legacy QML: install avahi-daemon and disable apt Check-Date on first boot
        push(QStringLiteral("packages:"), cloud);
        push(QStringLiteral("- avahi-daemon"), cloud);
        push(QString(), cloud);
        push(QStringLiteral("apt:"), cloud);
        push(QStringLiteral("  preserve_sources_list: true"), cloud);
        push(QStringLiteral("  conf: |"), cloud);
        push(QStringLiteral("    Acquire {"), cloud);
        push(QStringLiteral("      Check-Date \"false\";"), cloud);
        push(QStringLiteral("    };"), cloud);
        push(QString(), cloud);
    }
    
    const QString timezone = settings.value("timezone").toString().trimmed();
    if (!timezone.isEmpty()) {
        push(QStringLiteral("timezone: ") + timezone, cloud);
    }
    
    // Parity with legacy QML: include keyboard model/layout when locale is set
    const QString keyboardLayout = settings.value("keyboard").toString().trimmed();
    if (!keyboardLayout.isEmpty()) {
        push(QStringLiteral("keyboard:"), cloud);
        push(QStringLiteral("  model: pc105"), cloud);
        push(QStringLiteral("  layout: \"") + keyboardLayout + QStringLiteral("\""), cloud);
    }
    
    const bool sshPasswordAuth = settings.value("sshPasswordAuth").toBool();
    const QString userName = settings.value("sshUserName").toString().trimmed();
    const QString userPass = settings.value("sshUserPassword").toString(); // expected crypted if present
    const QString sshPublicKey = settings.value("sshPublicKey").toString().trimmed();
    const QString sshAuthorizedKeys = settings.value("sshAuthorizedKeys").toString().trimmed();
    
    // User configuration is independent of SSH - generate users section when:
    // - A username with password is configured (local user account), OR
    // - SSH is enabled with public keys (key-only authentication needs a user)
    const bool hasUserCredentials = !userName.isEmpty() && !userPass.isEmpty();
    const bool hasSshKeys = sshEnabled && (!sshPublicKey.isEmpty() || !sshAuthorizedKeys.isEmpty());
    
    if (hasUserCredentials || hasSshKeys) {
        push(QStringLiteral("users:"), cloud);
        // Determine effective username:
        // - Use provided username if available
        // - For SSH-only with keys, fall back to current system user or "pi"
        const QString effectiveUser = userName.isEmpty() ? (sshEnabled ? currentUser : QStringLiteral("pi")) : userName;
        push(QStringLiteral("- name: ") + effectiveUser, cloud);
        push(QStringLiteral("  groups: users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo"), cloud);
        push(QStringLiteral("  shell: /bin/bash"), cloud);
        
        if (!userPass.isEmpty()) {
            push(QStringLiteral("  lock_passwd: false"), cloud);
            // Quote the password hash to ensure proper YAML parsing
            // (consistent with network-config password handling)
            push(QStringLiteral("  passwd: \"") + userPass + QStringLiteral("\""), cloud);
        } else if (hasSshKeys) {
            // No password but SSH keys configured - lock password login
            push(QStringLiteral("  lock_passwd: true"), cloud);
        }
        
        // Include SSH authorized keys when SSH is enabled with keys
        if (hasSshKeys) {
            push(QStringLiteral("  ssh_authorized_keys:"), cloud);
            if (!sshAuthorizedKeys.isEmpty()) {
                const QStringList keys = sshAuthorizedKeys.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
                for (const QString& k : keys) {
                    push(QStringLiteral("    - ") + k.trimmed(), cloud);
                }
            } else {
                // Split sshPublicKey by newlines to handle .pub files with multiple keys
                const QStringList keys = sshPublicKey.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
                for (const QString& k : keys) {
                    push(QStringLiteral("    - ") + k.trimmed(), cloud);
                }
            }
            push(QStringLiteral("  sudo: ALL=(ALL) NOPASSWD:ALL"), cloud);
        }
        push(QString(), cloud); // blank line
    }
    
    // SSH daemon configuration - separate from user creation
    if (sshEnabled) {
        push(QStringLiteral("enable_ssh: true"), cloud);
        
        if (sshPasswordAuth) {
            push(QStringLiteral("ssh_pwauth: true"), cloud);
        } else if (!sshAuthorizedKeys.isEmpty() || !sshPublicKey.isEmpty()) {
            // Explicitly disable password authentication when using public-key auth
            push(QStringLiteral("ssh_pwauth: false"), cloud);
        }
    }
    
    const bool enableI2C = settings.value("enableI2C").toBool();
    const bool enableSPI = settings.value("enableSPI").toBool();
    const bool enable1Wire = settings.value("enable1Wire").toBool();
    const QString enableSerial = settings.value("enableSerial").toString();
    const bool armInterfaceEnabled = enableI2C || enableSPI || enable1Wire || enableSerial != "Disabled";
    const bool isUsbGadgetEnabled = settings.value("enableUsbGadget").toBool();
    
    // cc_raspberry_pi config for capable OSs
    if (hasCcRpi && (isUsbGadgetEnabled || armInterfaceEnabled)) {
        push(QStringLiteral("rpi:"), cloud);
        
        if (isUsbGadgetEnabled) {
            push(QStringLiteral("  enable_usb_gadget: true"), cloud);
        }
        
        // configure arm interfaces
        if (armInterfaceEnabled) {
            push(QStringLiteral("  interfaces:"), cloud);
            
            if (enableI2C) {
                push(QStringLiteral("    i2c: true"), cloud);
            }
            if (enableSPI) {
                push(QStringLiteral("    spi: true"), cloud);
            }
            if (enable1Wire) {
                push(QStringLiteral("    onewire: true"), cloud);
            }
            if (enableSerial != "Disabled") {
                if (enableSerial == "" || enableSerial == "Default") {
                    push(QStringLiteral("    serial: true"), cloud);
                } else {
                    push(QStringLiteral("    serial:"), cloud);
                    if (enableSerial == "Console & Hardware") {
                        push(QStringLiteral("      console: true"), cloud);
                        push(QStringLiteral("      hardware: true"), cloud);
                    } else if (enableSerial == "Console") {
                        push(QStringLiteral("      console: true"), cloud);
                        push(QStringLiteral("      hardware: false"), cloud);
                    } else if (enableSerial == "Hardware") {
                        push(QStringLiteral("      console: false"), cloud);
                        push(QStringLiteral("      hardware: true"), cloud);
                    }
                }
            }
        }
    }
    
    // Raspberry Pi Connect token provisioning via cloud-init write_files (store in user's home)
    const bool piConnectEnabled = settings.value("piConnectEnabled").toBool();
    QString cleanToken = piConnectToken.trimmed();
    const QString ssid = settings.value("wifiSSID").toString();
    const QString wifiCountry = settings.value("recommendedWifiCountry").toString().trimmed();
    
    // Determine if we need runcmd section
    // Only create runcmd if we actually have commands to add
    bool needsRuncmdForWifi = !wifiCountry.isEmpty() && ssid.isEmpty();
    bool needsRuncmdForPiConnect = piConnectEnabled && !cleanToken.isEmpty();
    bool needsRuncmd = needsRuncmdForPiConnect || needsRuncmdForWifi;
    
    if (needsRuncmd) {
        push(QString(), cloud);
        push(QStringLiteral("runcmd:"), cloud);
        
        if (needsRuncmdForPiConnect) {
            // Use the same effective user logic as user configuration above
            const QString effectiveUser = userName.isEmpty() ? (sshEnabled ? currentUser : QStringLiteral("pi")) : userName;
            const QString configDir = QStringLiteral("/home/") + effectiveUser + QStringLiteral("/") + PI_CONNECT_CONFIG_PATH;
            const QString targetPath = configDir + QStringLiteral("/") + PI_CONNECT_DEPLOY_KEY_FILENAME;
            
            // Don't use write_files with defer:true because cloud-init tries to resolve the user/group
            // at parse time using getpwnam(), which fails if the user doesn't exist yet.
            // Instead, create the file via runcmd after the user is guaranteed to exist.
            // Create directory and file in runcmd to ensure user exists first
            push(QStringLiteral("  - [ sh, -c, \"install -o ") + effectiveUser + QStringLiteral(" -m 700 -d ") + configDir + QStringLiteral("\" ]"), cloud);
            // Write the token file using printf with single-quoted string (safest for arbitrary content)
            // Inside single quotes, only single quotes need escaping (break out, add escaped quote, resume)
            QString escapedToken = cleanToken;
            escapedToken.replace("'", "'\"'\"'");  // ' becomes '"'"' (end quote, add escaped quote, start quote)
            QString writeTokenCmd = QStringLiteral("  - [ sh, -c, \"printf '%s\\n' '") + escapedToken + QStringLiteral("' > ") + targetPath + QStringLiteral(" && chown ") + effectiveUser + QStringLiteral(":") + effectiveUser + QStringLiteral(" ") + targetPath + QStringLiteral(" && chmod 600 ") + targetPath + QStringLiteral("\" ]");
            push(writeTokenCmd, cloud);
            // Enable Raspberry Pi Connect systemd units
            QString userHome = QStringLiteral("/home/") + effectiveUser;
            push(QStringLiteral("  - [ sh, -c, \"install -o ") + effectiveUser + QStringLiteral(" -m 700 -d ") + userHome + QStringLiteral("/.config/systemd/user/default.target.wants ") + userHome + QStringLiteral("/.config/systemd/user/paths.target.wants\" ]"), cloud);
            // Check both /usr/lib and /lib for systemd unit files (different distros use different paths)
            push(QStringLiteral("  - [ sh, -c, \"UNIT_SRC=/usr/lib/systemd/user/rpi-connect.service; [ -f $UNIT_SRC ] || UNIT_SRC=/lib/systemd/user/rpi-connect.service; ln -sf $UNIT_SRC ") + userHome + QStringLiteral("/.config/systemd/user/default.target.wants/rpi-connect.service\" ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"UNIT_SRC=/usr/lib/systemd/user/rpi-connect-signin.path; [ -f $UNIT_SRC ] || UNIT_SRC=/lib/systemd/user/rpi-connect-signin.path; ln -sf $UNIT_SRC ") + userHome + QStringLiteral("/.config/systemd/user/paths.target.wants/rpi-connect-signin.path\" ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"UNIT_SRC=/usr/lib/systemd/user/rpi-connect-wayvnc.service; [ -f $UNIT_SRC ] || UNIT_SRC=/lib/systemd/user/rpi-connect-wayvnc.service; ln -sf $UNIT_SRC ") + userHome + QStringLiteral("/.config/systemd/user/default.target.wants/rpi-connect-wayvnc.service\" ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"chown -R ") + effectiveUser + QStringLiteral(":") + effectiveUser + QStringLiteral(" ") + userHome + QStringLiteral("/.config/systemd\" ]"), cloud);
            // Set up systemd linger for auto-start
            push(QStringLiteral("  - [ sh, -c, \"install -d -m 0755 /var/lib/systemd/linger\" ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"install -m 0644 /dev/null /var/lib/systemd/linger/") + effectiveUser + QStringLiteral("\" ]"), cloud);
            // Start the user services now (linger ensures user manager is running)
            push(QStringLiteral("  - [ sh, -c, \"loginctl enable-linger ") + effectiveUser + QStringLiteral(" 2>/dev/null || true\" ]"), cloud);
            push(QStringLiteral("  - [ sleep, \"2\" ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"systemctl --quiet --user --machine=") + effectiveUser + QStringLiteral("@.host daemon-reload || true\" ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"systemctl --quiet --user --machine=") + effectiveUser + QStringLiteral("@.host start rpi-connect.service || true\" ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"systemctl --quiet --user --machine=") + effectiveUser + QStringLiteral("@.host start rpi-connect-signin.path || true\" ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"systemctl --quiet --user --machine=") + effectiveUser + QStringLiteral("@.host start rpi-connect-wayvnc.service || true\" ]"), cloud);
        }
        
        if (needsRuncmdForWifi) {
            // When Wi-Fi country is set but no SSID, unblock Wi-Fi to prevent "blocked by rfkill" message
            push(QStringLiteral("  - [ rfkill, unblock, wifi ]"), cloud);
            push(QStringLiteral("  - [ sh, -c, \"for f in /var/lib/systemd/rfkill/*:wlan; do echo 0 > \\\"$f\\\"; done\" ]"), cloud);
        }
    }
    
    return cloud;
}

QByteArray CustomisationGenerator::generateCloudInitNetworkConfig(const QVariantMap& settings,
                                                                  bool hasCcRpi)
{
    QByteArray netcfg;
    
    auto push = [](const QString& line, QByteArray& out) {
        if (!line.isEmpty()) {
            out += line.toUtf8();
            out += '\n';
        }
    };
    
    const QString ssid = settings.value("wifiSSID").toString();
    const QString cryptedPskFromSettings = settings.value("wifiPasswordCrypt").toString();
    const bool hidden = settings.value("wifiHidden").toBool();
    const QString regDom = settings.value("recommendedWifiCountry").toString().trimmed().toUpper();
    
    // Always generate network config with eth0 DHCP configuration
    // This ensures wired ethernet works out of the box with both IPv4 and IPv6
    push(QStringLiteral("network:"), netcfg);
    push(QStringLiteral("  version: 2"), netcfg);
    
    // Configure eth0 with DHCP for both IPv4 and IPv6
    push(QStringLiteral("  ethernets:"), netcfg);
    push(QStringLiteral("    eth0:"), netcfg);
    push(QStringLiteral("      dhcp4: true"), netcfg);
    push(QStringLiteral("      dhcp6: true"), netcfg);
    push(QStringLiteral("      optional: true"), netcfg);
    
    // Generate WiFi config if we have an SSID
    // Cloud-init requires at least one access-point if wifis: is defined, so we can't
    // generate wifis config with just a regulatory domain. When we have an SSID, we set
    // the regulatory domain in the network config here. When there's no SSID, the regulatory
    // domain is set via cmdline parameter (cfg80211.ieee80211_regdom) in imagewriter.cpp
    if (!ssid.isEmpty()) {
        push(QStringLiteral("  wifis:"), netcfg);
        push(QStringLiteral("    wlan0:"), netcfg);
        push(QStringLiteral("      dhcp4: true"), netcfg);
        if (!regDom.isEmpty()) {
            push(QStringLiteral("      regulatory-domain: \"") + regDom + QStringLiteral("\""), netcfg);
        }
        
        push(QStringLiteral("      access-points:"), netcfg);
        // Escape SSID for YAML double-quoted string context
        // Per IEEE 802.11, SSIDs can contain any byte value (0-255), including
        // backslashes, quotes, control characters, and non-printable bytes
        push(QStringLiteral("        \"") + yamlEscapeString(ssid) + QStringLiteral("\":"), netcfg);
        if (hidden) {
            push(QStringLiteral("          hidden: true"), netcfg);
        }
        // Prefer persisted crypted PSK; fallback to deriving from legacy plaintext setting
        QString effectiveCryptedPsk = cryptedPskFromSettings;
        if (effectiveCryptedPsk.isEmpty()) {
            const QString legacyPwd = settings.value("wifiPassword").toString();
            if (!legacyPwd.isEmpty()) {
                const bool isPassphrase = (legacyPwd.length() >= 8 && legacyPwd.length() < 64);
                effectiveCryptedPsk = isPassphrase ? pbkdf2(legacyPwd.toUtf8(), ssid.toUtf8()) : legacyPwd;
            }
        }
        // Required because without a password and hidden netplan would read ssid: null and crash
        effectiveCryptedPsk.replace('"', QStringLiteral("\\\""));
        // Use password shorthand at access-point level (not inside auth: block)
        // This makes netplan automatically enable WPA2/WPA3 transition mode with PMF optional
        // See: https://github.com/canonical/netplan/blob/main/src/parse.c (handle_access_point_password)
        push(QStringLiteral("          password: \"") + effectiveCryptedPsk + QStringLiteral("\""), netcfg);
        
        push(QStringLiteral("      optional: true"), netcfg);
    }
    
    return netcfg;
}

} // namespace rpi_imager

