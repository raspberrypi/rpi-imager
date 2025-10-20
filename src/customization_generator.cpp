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
    const QString wifiCountry = s.value("wifiCountry").toString().trimmed();
    
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
        keyList.append(sshPublicKey);
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
    if (!userName.isEmpty() || !userPass.isEmpty()) {
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
        line(QStringLiteral("\tpsk=") + cryptedPsk, script);
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
        // Determine home directory for the effective user
        line(QStringLiteral("TARGET_USER=\"") + effectiveUser + QStringLiteral("\""), script);
        line(QStringLiteral("TARGET_HOME=$(getent passwd \"$TARGET_USER\" | cut -d: -f6)"), script);
        line(QStringLiteral("if [ -z \"$TARGET_HOME\" ] || [ ! -d \"$TARGET_HOME\" ]; then TARGET_HOME=\"/home/") + effectiveUser + QStringLiteral("\"; fi"), script);
        line(QStringLiteral("install -o \"$TARGET_USER\" -m 700 -d \"$TARGET_HOME/com.raspberrypi.connect\""), script);
        line(QStringLiteral("cat > \"$TARGET_HOME/com.raspberrypi.connect/deploy.key\" <<'EOF'"), script);
        line(piConnectTokenTrimmed, script);
        line(QStringLiteral("EOF"), script);
        line(QStringLiteral("chown \"$TARGET_USER:$TARGET_USER\" \"$TARGET_HOME/com.raspberrypi.connect/deploy.key\""), script);
        line(QStringLiteral("chmod 600 \"$TARGET_HOME/com.raspberrypi.connect/deploy.key\""), script);

        // Enable systemd user service rpi-connect-signin.service for the target user
        line(QStringLiteral("install -o \"$TARGET_USER\" -m 700 -d \"$TARGET_HOME/.config/systemd/user/default.target.wants\""), script);
        line(QStringLiteral("UNIT_SRC=\"/usr/lib/systemd/user/rpi-connect-signin.service\"; [ -f \"$UNIT_SRC\" ] || UNIT_SRC=\"/lib/systemd/user/rpi-connect-signin.service\""), script);
        line(QStringLiteral("ln -sf \"$UNIT_SRC\" \"$TARGET_HOME/.config/systemd/user/default.target.wants/rpi-connect-signin.service\""), script);
        line(QStringLiteral("chown -R \"$TARGET_USER:$TARGET_USER\" \"$TARGET_HOME/.config/systemd\" || true"), script);
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
    
    const QString hostname = settings.value("hostname").toString().trimmed();
    if (!hostname.isEmpty()) {
        push(QStringLiteral("hostname: ") + hostname, cloud);
        push(QStringLiteral("manage_etc_hosts: true"), cloud);
        // Parity with legacy QML: install avahi-daemon and disable apt Check-Date on first boot
        push(QStringLiteral("packages:"), cloud);
        push(QStringLiteral("- avahi-daemon"), cloud);
        push(QString(), cloud);
        push(QStringLiteral("apt:"), cloud);
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
    
    if (sshEnabled) {
        push(QStringLiteral("enable_ssh: true"), cloud);
        
        if (!userName.isEmpty()) {
            push(QStringLiteral("users:"), cloud);
            // Parity: legacy QML used the typed username even when not renaming the user.
            // Fall back to getCurrentUser() when SSH is enabled and no explicit username was saved.
            const QString effectiveUser = userName.isEmpty() && sshEnabled ? currentUser : (userName.isEmpty() ? QStringLiteral("pi") : userName);
            push(QStringLiteral("- name: ") + effectiveUser, cloud);
            push(QStringLiteral("  groups: users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo"), cloud);
            push(QStringLiteral("  shell: /bin/bash"), cloud);
            if (!userPass.isEmpty()) {
                push(QStringLiteral("  lock_passwd: false"), cloud);
                push(QStringLiteral("  passwd: ") + userPass, cloud);
            } else if (!sshPublicKey.isEmpty() || !sshAuthorizedKeys.isEmpty()) {
                push(QStringLiteral("  lock_passwd: true"), cloud);
            }
            // Include all authorised keys (multi-line) if provided, else fall back to single key
            if (!sshAuthorizedKeys.isEmpty() || !sshPublicKey.isEmpty()) {
                push(QStringLiteral("  ssh_authorized_keys:"), cloud);
                if (!sshAuthorizedKeys.isEmpty()) {
                    const QStringList keys = sshAuthorizedKeys.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
                    for (const QString& k : keys) {
                        push(QStringLiteral("    - ") + k.trimmed(), cloud);
                    }
                } else {
                    push(QStringLiteral("    - ") + sshPublicKey, cloud);
                }
                push(QStringLiteral("  sudo: ALL=(ALL) NOPASSWD:ALL"), cloud);
            }
            push(QString(), cloud); // blank line
        }
        
        if (sshPasswordAuth) {
            push(QStringLiteral("ssh_pwauth: true"), cloud);
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
    const QString wifiCountry = settings.value("wifiCountry").toString().trimmed();
    
    // Determine if we need runcmd section
    bool needsRuncmd = (piConnectEnabled && !cleanToken.isEmpty()) || 
                       (!wifiCountry.isEmpty() && ssid.isEmpty());
    
    if (piConnectEnabled && !cleanToken.isEmpty()) {
        // Use the same effective user decision as above
        const QString effectiveUser = userName.isEmpty() && sshEnabled ? currentUser : (userName.isEmpty() ? QStringLiteral("pi") : userName);
        const QString targetPath = QStringLiteral("/home/") + effectiveUser + QStringLiteral("/com.raspberrypi.connect/auth.key");
        push(QStringLiteral("write_files:"), cloud);
        push(QStringLiteral("  - path: ") + targetPath, cloud);
        push(QStringLiteral("    permissions: '0600'"), cloud);
        push(QStringLiteral("    owner: ") + effectiveUser + QStringLiteral(":") + effectiveUser, cloud);
        push(QStringLiteral("    content: |"), cloud);
        QString indented = QStringLiteral("      ") + cleanToken;
        push(indented, cloud);
        // Ensure directory exists with correct owner
        push(QString(), cloud);
        push(QStringLiteral("runcmd:"), cloud);
        push(QStringLiteral("  - [ sh, -c, \"install -o ") + effectiveUser + QStringLiteral(" -m 700 -d /home/") + effectiveUser + QStringLiteral("/com.raspberrypi.connect\" ]"), cloud);
    } else if (needsRuncmd) {
        // Start runcmd section if not already started
        push(QStringLiteral("runcmd:"), cloud);
    }
    
    // When Wi-Fi country is set but no SSID, unblock Wi-Fi to prevent "blocked by rfkill" message
    if (!wifiCountry.isEmpty() && ssid.isEmpty()) {
        push(QStringLiteral("  - [ rfkill, unblock, wifi ]"), cloud);
        push(QStringLiteral("  - [ sh, -c, \"for f in /var/lib/systemd/rfkill/*:wlan; do echo 0 > \\\"$f\\\"; done\" ]"), cloud);
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
    const QString regDom = settings.value("wifiCountry").toString().trimmed().toUpper();
    
    // Generate network config if we have a country code (regulatory domain) or an SSID
    if (!regDom.isEmpty() || !ssid.isEmpty()) {
        push(QStringLiteral("network:"), netcfg);
        push(QStringLiteral("  version: 2"), netcfg);
        push(QStringLiteral("  wifis:"), netcfg);
        push(QStringLiteral("    wlan0:"), netcfg);
        push(QStringLiteral("      dhcp4: true"), netcfg);
        if (!regDom.isEmpty()) {
            push(QStringLiteral("      regulatory-domain: \"") + regDom + QStringLiteral("\""), netcfg);
        }
        
        // Only configure access points if we have an SSID
        if (!ssid.isEmpty()) {
            push(QStringLiteral("      access-points:"), netcfg);
            {
                QString key = ssid;
                key.replace('"', QStringLiteral("\\\""));
                push(QStringLiteral("        \"") + key + QStringLiteral("\":"), netcfg);
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
            push(QStringLiteral("          password: \"") + effectiveCryptedPsk + QStringLiteral("\""), netcfg);
            if (hidden) {
                push(QStringLiteral("          hidden: true"), netcfg);
            }
        }
        
        push(QStringLiteral("      optional: true"), netcfg);
    }
    
    return netcfg;
}

} // namespace rpi_imager

