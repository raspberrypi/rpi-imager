/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "customization_generator.h"
#include "dependencies/sha256crypt/sha256crypt.h"
#include "dependencies/yescrypt/yescrypt_wrapper.h"
#include <QPasswordDigestor>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>
#include <QStringConverter>
#include <QDebug>
#include <random>

namespace rpi_imager {

namespace {

bool isValidUtf8(const QByteArray& data)
{
    if (data.isEmpty())
        return true;
    // QStringDecoder::operator() returns a lazy proxy; the decode (and therefore
    // error detection) only runs when it is materialised into a QString. The
    // result must be consumed or hasError() always stays false and arbitrary
    // octets are wrongly reported as valid UTF-8. Flag::Stateless additionally
    // makes a trailing incomplete multi-byte sequence an error, which is correct
    // when validating a complete SSID/string buffer.
    auto converter = QStringDecoder(QStringConverter::Utf8,
                                    QStringConverter::Flag::Stateless);
    const QString decoded = converter(data);
    Q_UNUSED(decoded)
    return !converter.hasError();
}

bool ssidOctetsSafeForImagerCustom(const QByteArray& ssidOctets)
{
    if (ssidOctets.contains('\0'))
        return false;
    return isValidUtf8(ssidOctets);
}

// tomlQuote VALUE — render VALUE as an rpi-preseed TOML basic string.
// rpi-preseed's parser only unescapes \\ and \" and keeps values single-line,
// so we escape exactly those two characters and drop any embedded newlines
// (callers already split multi-value inputs such as SSH keys line-by-line).
QString tomlQuote(const QString& value)
{
    QString v = value;
    v.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    v.replace(QLatin1Char('"'), QLatin1String("\\\""));
    v.remove(QLatin1Char('\r'));
    v.remove(QLatin1Char('\n'));
    return QLatin1Char('"') + v + QLatin1Char('"');
}

// tomlIsHex VALUE — true if VALUE is a non-empty run of hex digits. Used to tell
// a raw 64-char PMK (encrypted PSK) apart from a plaintext Wi-Fi passphrase.
bool tomlIsHex(const QString& value)
{
    if (value.isEmpty())
        return false;
    for (const QChar qc : value) {
        const char c = qc.toLatin1();
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex)
            return false;
    }
    return true;
}

QByteArray wpaSupplicantSsidField(const QByteArray& ssidOctets)
{
    auto needsHex = [&]() {
        if (!isValidUtf8(ssidOctets))
            return true;
        for (unsigned char byte : ssidOctets) {
            if (byte < 0x20 || byte == 0x7F || byte == '\\' || byte == '"')
                return true;
        }
        return false;
    }();

    if (needsHex)
        return QByteArray("\tssid=hex:") + ssidOctets.toHex();

    QByteArray escaped("\tssid=\"");
    for (unsigned char byte : ssidOctets) {
        if (byte == '\\')
            escaped += "\\\\";
        else if (byte == '"')
            escaped += "\\\"";
        else
            escaped += char(byte);
    }
    escaped += '"';
    return escaped;
}

} // namespace

QString CustomisationGenerator::shellQuote(const QString& value) {
    QString t = value;
    t.replace("'", "'\"'\"'");
    return QString("'") + t + QString("'");
}

QString CustomisationGenerator::pbkdf2(const QByteArray& password, const QByteArray& ssid) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha1, password, ssid, 4096, 32).toHex();
}

QString CustomisationGenerator::cryptPassword(const QByteArray& passwordInput, const QString& osReleaseDate) {
    // Strip CR/LF before hashing. Pasted clipboard content can carry a trailing
    // newline (single-line text fields do not sanitise pasted text), and PAM
    // always discards the line terminator when reading a password. Hashing the
    // raw value would then produce a hash that can never be matched at login
    // (see issue #1627). These characters can never form part of a usable
    // password, so removing them everywhere is safe.
    QByteArray password = passwordInput;
    password.removeIf([](char c) { return c == '\r' || c == '\n'; });

    const QByteArray saltchars =
      "./0123456789ABCDEFGHIJKLMNOPQRST"
      "UVWXYZabcdefghijklmnopqrstuvwxyz";
    std::mt19937 gen(static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch()));
    std::uniform_int_distribution<> uid(0, saltchars.length() - 1);

    if (osUsesYescrypt(osReleaseDate)) {
        QByteArray salt;
        for (int i = 0; i < 16; i++)  // yescrypt uses longer salts
            salt += saltchars[uid(gen)];

        char *result = yescrypt_crypt(password.constData(), salt.constData());
        return result ? QString(result) : QString();
    }

    QByteArray salt = "$5$";
    for (int i = 0; i < 10; i++)
        salt += saltchars[uid(gen)];

    return sha256_crypt(password.constData(), salt.constData());
}

bool CustomisationGenerator::osUsesYescrypt(const QString& osReleaseDate) {
    if (osReleaseDate.isEmpty())
        return false;
    const QDate releaseDate = QDate::fromString(osReleaseDate, "yyyy-MM-dd");
    const QDate cutoffDate(2023, 1, 1);
    return releaseDate.isValid() && releaseDate >= cutoffDate;
}

bool CustomisationGenerator::isYescryptHash(const QString& cryptHash) {
    // We only ever emit "$y$" (yescrypt) or "$5$" (sha256crypt). "$7$" is the
    // other yescrypt/scrypt prefix the wrapper accepts, so treat it as yescrypt too.
    return cryptHash.startsWith(QStringLiteral("$y$"))
        || cryptHash.startsWith(QStringLiteral("$7$"));
}

QString CustomisationGenerator::resolveUserPasswordCrypt(const QVariantMap& settings) {
    const QString plain = settings.value(QStringLiteral("sshUserPasswordPlain")).toString();
    if (!plain.isEmpty()) {
        const QString releaseDate = settings.value(QStringLiteral("osReleaseDate")).toString();
        return cryptPassword(plain.toUtf8(), releaseDate);
    }
    // Already-crypted password reused from saved settings. The UI is expected to
    // have invalidated any hash that is incompatible with the selected OS (a
    // yescrypt hash cannot authenticate on a pre-2023 image); if one slips
    // through here - e.g. the OS was changed after the user step was skipped -
    // log it loudly, since we have no plaintext left to re-hash.
    const QString crypted = settings.value(QStringLiteral("sshUserPassword")).toString();
    if (!crypted.isEmpty() && isYescryptHash(crypted)
        && !osUsesYescrypt(settings.value(QStringLiteral("osReleaseDate")).toString())) {
        qWarning() << "Account password hash is yescrypt but the selected OS predates "
                      "yescrypt support; login may fail. The password should have been "
                      "re-entered for this OS.";
    }
    return crypted;
}

QString CustomisationGenerator::resolveWifiPskCrypt(const QVariantMap& settings, const QByteArray& ssidOctets, bool wifiConfigured) {
    if (!wifiConfigured)
        return {};

    const QString crypted = settings.value(QStringLiteral("wifiPasswordCrypt")).toString();
    if (!crypted.isEmpty())
        return crypted;

    const QString plain = settings.value(QStringLiteral("wifiPassword")).toString();
    if (plain.isEmpty())
        return {};

    // Passphrase length per WPA spec is 8..63; anything else is taken to be a
    // pre-computed 64-hex-digit PSK (or open-network sentinel) and passed through.
    const bool isPassphrase = (plain.length() >= 8 && plain.length() < 64);
    return isPassphrase ? pbkdf2(plain.toUtf8(), ssidOctets) : plain;
}

QByteArray CustomisationGenerator::yamlEscapeSsidOctets(const QByteArray& value)
{
    QByteArray result;
    result.reserve(value.size() * 2);

    for (unsigned char byte : value) {
        switch (byte) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case 0:
            result += "\\0";
            break;
        default:
            if (byte >= 0x20 && byte < 0x7F)
                result += char(byte);
            else {
                result += "\\x";
                result += QByteArray::number(byte, 16).rightJustified(2, '0');
            }
            break;
        }
    }

    return result;
}

QByteArray CustomisationGenerator::ssidOctetsFromSettings(const QVariantMap& settings, bool wifiConfigured)
{
    if (!wifiConfigured)
        return {};

    if (settings.contains(QStringLiteral("wifiSsidOctets"))) {
        const QVariant raw = settings.value(QStringLiteral("wifiSsidOctets"));
        if (raw.metaType().id() == QMetaType::QByteArray)
            return raw.toByteArray();
    }

    if (settings.contains(QStringLiteral("wifiSsidOctetsBase64"))) {
        return QByteArray::fromBase64(settings.value(QStringLiteral("wifiSsidOctetsBase64")).toByteArray());
    }

    return settings.value(QStringLiteral("wifiSSID")).toString().toUtf8();
}

QString CustomisationGenerator::yamlEscapeString(const QString& value) {
    return QString::fromUtf8(yamlEscapeSsidOctets(value.toUtf8()));
}

QByteArray CustomisationGenerator::generateSystemdScript(const QVariantMap& s, const QString& piConnectToken) {
    QByteArray script;
    auto line = [](const QString& l, QByteArray& out) { out += l.toUtf8(); out += '\n'; };

    const QString hostname = s.value("hostname").toString().trimmed();
    const QString timezone = s.value("timezone").toString().trimmed();
    const bool sshEnabled = s.value("sshEnabled").toBool();
    const bool sshPasswordAuth = s.value("sshPasswordAuth").toBool();
    const QString userName = s.value("sshUserName").toString().trimmed();
    const QString userPass = resolveUserPasswordCrypt(s); // crypted (hashes plaintext if present)
    const QString sshPublicKey = s.value("sshPublicKey").toString().trimmed();
    const QString sshAuthorizedKeys = s.value("sshAuthorizedKeys").toString().trimmed();
    const bool wifiConfigured = s.value("wifiConfigured", true).toBool();
    const QByteArray ssidOctets = ssidOctetsFromSettings(s, wifiConfigured);
    bool hidden = wifiConfigured ? s.value("wifiHidden").toBool() : false;
    if (!hidden)
        hidden = s.value("wifiSSIDHidden").toBool();
    const QString wifiCountry = s.value("recommendedWifiCountry").toString().trimmed();

    // Crypted PSK: prefer a pre-derived value, else derive from the plaintext passphrase.
    const QString cryptedPsk = resolveWifiPskCrypt(s, ssidOctets, wifiConfigured);
    
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

    const bool passwordlessSudo = s.value("passwordlessSudo").toBool();
    const QString effectiveUser = userName.isEmpty() ? QStringLiteral("pi") : userName;

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

    // Passwordless sudo configuration
    if (passwordlessSudo && (!userName.isEmpty() || !userPass.isEmpty() || !keyList.isEmpty())) {
        const QString sudoersFile = QStringLiteral("/etc/sudoers.d/010_") + effectiveUser + QStringLiteral("-nopasswd");
        line(QStringLiteral("echo ") + shellQuote(effectiveUser + QStringLiteral(" ALL=(ALL) NOPASSWD:ALL")) + QStringLiteral(" >") + shellQuote(sudoersFile), script);
        line(QStringLiteral("chmod 0440 ") + shellQuote(sudoersFile), script);
    }

    if (!ssidOctets.isEmpty()) {
        const bool useImagerCustom = ssidOctetsSafeForImagerCustom(ssidOctets);
        // Prefer imager_custom set_wlan when the SSID is shell-safe UTF-8; otherwise
        // write wpa_supplicant.conf directly with hex/raw octets.
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ] && ")
             + (useImagerCustom ? QStringLiteral("true") : QStringLiteral("false"))
             + QStringLiteral("; then"), script);
        if (useImagerCustom) {
            QString wlanCmd = QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_wlan ");
            if (hidden) wlanCmd += QStringLiteral(" -h ");
            wlanCmd += shellQuote(QString::fromUtf8(ssidOctets)) + QStringLiteral(" ")
                     + shellQuote(cryptedPsk) + QStringLiteral(" ") + shellQuote(wifiCountry);
            line(wlanCmd, script);
        }
        line(QStringLiteral("else"), script);
        script += "cat >/etc/wpa_supplicant/wpa_supplicant.conf <<'WPAEOF'\n";
        if (!wifiCountry.isEmpty())
            script += ("country=" + wifiCountry + "\n").toUtf8();
        script += "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n";
        script += "ap_scan=1\n\n";
        script += "update_config=1\n";
        script += "network={\n";
        if (hidden)
            script += "\tscan_ssid=1\n";
        script += wpaSupplicantSsidField(ssidOctets);
        script += "\n";
        if (cryptedPsk.isEmpty()) {
            script += "\tkey_mgmt=NONE\n";
        } else {
            script += "\tkey_mgmt=WPA-PSK SAE\n";
            script += "\tpsk=" + cryptedPsk.toUtf8() + "\n";
            script += "\tieee80211w=1\n";
        }
        script += "}\n";
        script += "WPAEOF\n";
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
    const QString userPass = resolveUserPasswordCrypt(settings); // crypted (hashes plaintext if present)
    const QString sshPublicKey = settings.value("sshPublicKey").toString().trimmed();
    const QString sshAuthorizedKeys = settings.value("sshAuthorizedKeys").toString().trimmed();
    const bool passwordlessSudo = settings.value("passwordlessSudo").toBool();
    
    // User configuration is independent of SSH - generate users section when:
    // - A username with password is configured (local user account), OR
    // - SSH is enabled with public keys (key-only authentication needs a user)
    const bool hasUserCredentials = !userName.isEmpty() && !userPass.isEmpty();
    const bool hasSshKeys = sshEnabled && (!sshPublicKey.isEmpty() || !sshAuthorizedKeys.isEmpty());

    // Determine effective username (used by both user: section and runcmd)
    const QString effectiveUser = userName.isEmpty() ? (sshEnabled ? currentUser : QStringLiteral("pi")) : userName;
    const bool needsRuncmdForSudo = passwordlessSudo && (hasUserCredentials || hasSshKeys);

    if (hasUserCredentials || hasSshKeys) {
        // Use singular `user:` so cloud-init renames/extends the distro's
        // default user and preserves its group memberships (sudo, adm, gpio,
        // etc.). The plural `users:` list without a `- default` entry bypasses
        // the distro default and produces a group-less account - see
        // https://github.com/raspberrypi/rpi-imager/issues/1601.
        push(QStringLiteral("user:"), cloud);
        push(QStringLiteral("  name: ") + effectiveUser, cloud);
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
                    push(QStringLiteral("    - \"") + k.trimmed() + QStringLiteral("\""), cloud);
                }
            } else {
                // Split sshPublicKey by newlines to handle .pub files with multiple keys
                const QStringList keys = sshPublicKey.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
                for (const QString& k : keys) {
                    push(QStringLiteral("    - \"") + k.trimmed() + QStringLiteral("\""), cloud);
                }
            }
        }
        if (passwordlessSudo) {
            push(QStringLiteral("  sudo: ALL=(ALL) NOPASSWD:ALL"), cloud);
        }
        push(QString(), cloud); // blank line
    }
    
    // SSH daemon configuration - separate from user creation.
    // cc_ssh only writes host keys / authorized_keys; it does not enable the
    // sshd unit. Ubuntu cloud images ship sshd enabled, but RPi OS does not -
    // so we activate the unit via runcmd below. The (non-existent in upstream
    // cloud-init) `enable_ssh: true` key was dropped because it was silently
    // ignored on every distro.
    if (sshEnabled) {
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
    const bool serialEnabled = !enableSerial.isEmpty() && enableSerial != "Disabled";
    const bool armInterfaceEnabled = enableI2C || enableSPI || enable1Wire || serialEnabled;
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
            if (serialEnabled) {
                if (enableSerial == "Default") {
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
    const bool wifiConfigured = settings.value("wifiConfigured", true).toBool();
    const QByteArray ssidOctets = ssidOctetsFromSettings(settings, wifiConfigured);
    const QString wifiCountry = settings.value("recommendedWifiCountry").toString().trimmed();
    
    // Determine if we need runcmd section
    // Only create runcmd if we actually have commands to add
    bool needsRuncmdForWifi = !wifiCountry.isEmpty() && ssidOctets.isEmpty();
    bool needsRuncmdForPiConnect = piConnectEnabled && !cleanToken.isEmpty();
    bool needsRuncmdForSsh = sshEnabled;
    bool needsRuncmd = needsRuncmdForPiConnect || needsRuncmdForWifi || needsRuncmdForSudo || needsRuncmdForSsh;

    if (needsRuncmd) {
        push(QString(), cloud);
        push(QStringLiteral("runcmd:"), cloud);

        if (needsRuncmdForSsh) {
            // cc_ssh writes host keys / authorized_keys but never starts sshd.
            // RPi OS images ship sshd disabled, so enable it explicitly here.
            // No-op on Ubuntu Server where ssh is already enabled.
            push(QStringLiteral("  - [ systemctl, enable, --now, ssh ]"), cloud);
        }

        // Passwordless sudo: create sudoers file explicitly via runcmd.
        // The sudo: user property works on standard cloud-init but is not
        // reliably processed by all implementations (e.g. cc_raspberry_pi).
        // Creating the file directly matches the systemd firstrun.sh approach.
        if (needsRuncmdForSudo) {
            const QString sudoersFile = QStringLiteral("/etc/sudoers.d/010_") + effectiveUser + QStringLiteral("-nopasswd");
            push(QStringLiteral("  - [ sh, -c, \"echo '") + effectiveUser + QStringLiteral(" ALL=(ALL) NOPASSWD:ALL' >") + sudoersFile + QStringLiteral("\" ]"), cloud);
            push(QStringLiteral("  - [ chmod, '0440', '") + sudoersFile + QStringLiteral("' ]"), cloud);
        }

        if (needsRuncmdForPiConnect) {
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
    
    // Don't let cloud-init manage DNS - let dhcpcd/NetworkManager handle it
    // Only include this when there is actual customisation content, so that
    // skipping customisation produces an empty payload.
    if (!cloud.isEmpty()) {
        cloud.prepend("manage_resolv_conf: false\n\n");
    }

    return cloud;
}

QByteArray CustomisationGenerator::generateCloudInitNetworkConfig(const QVariantMap& settings,
                                                                  bool hasCcRpi)
{
    Q_UNUSED(hasCcRpi);

    QByteArray netcfg;
    
    auto push = [](const QString& line, QByteArray& out) {
        if (!line.isEmpty()) {
            out += line.toUtf8();
            out += '\n';
        }
    };
    auto pushBytes = [](const QByteArray& line, QByteArray& out) {
        if (!line.isEmpty()) {
            out += line;
            out += '\n';
        }
    };
    
    const bool wifiConfigured = settings.value("wifiConfigured", true).toBool();
    const QByteArray ssidOctets = ssidOctetsFromSettings(settings, wifiConfigured);
    const bool hidden = wifiConfigured ? settings.value("wifiHidden").toBool() : false;
    const QString regDom = settings.value("recommendedWifiCountry").toString().trimmed().toUpper();
    
    // Generate WiFi config if we have an SSID
    // Cloud-init requires at least one access-point if wifis: is defined, so we can't
    // generate wifis config with just a regulatory domain. When we have an SSID, we set
    // the regulatory domain in the network config here. When there's no SSID, the regulatory
    // domain is set via cmdline parameter (cfg80211.ieee80211_regdom) in imagewriter.cpp
    if (!ssidOctets.isEmpty()) {
        // Include eth0 DHCP alongside wifi to ensure wired ethernet continues to work
        // when cloud-init applies the network-config (which would otherwise replace the
        // distro default). Only needed when we're providing a network-config file.
        push(QStringLiteral("network:"), netcfg);
        push(QStringLiteral("  version: 2"), netcfg);
        push(QStringLiteral("  ethernets:"), netcfg);
        push(QStringLiteral("    eth0:"), netcfg);
        push(QStringLiteral("      dhcp4: true"), netcfg);
        push(QStringLiteral("      dhcp6: true"), netcfg);
        push(QStringLiteral("      optional: true"), netcfg);

        push(QStringLiteral("  wifis:"), netcfg);
        push(QStringLiteral("    wlan0:"), netcfg);
        push(QStringLiteral("      dhcp4: true"), netcfg);
        if (!regDom.isEmpty()) {
            push(QStringLiteral("      regulatory-domain: \"") + regDom + QStringLiteral("\""), netcfg);
        }
        
        push(QStringLiteral("      access-points:"), netcfg);
        // Escape SSID octets for YAML double-quoted string context. High bytes and
        // controls use \\xHH so netplan receives the exact IEEE 802.11 SSID bytes.
        {
            QByteArray keyLine("        \"");
            keyLine += yamlEscapeSsidOctets(ssidOctets);
            keyLine += "\":";
            pushBytes(keyLine, netcfg);
        }
        if (hidden) {
            push(QStringLiteral("          hidden: true"), netcfg);
        }
        // Crypted PSK: prefer a pre-derived value, else derive from the plaintext passphrase.
        QString effectiveCryptedPsk = resolveWifiPskCrypt(settings, ssidOctets, wifiConfigured);
        if (effectiveCryptedPsk.isEmpty()) {
            // Open network (no password) - use auth block with key-management: none
            // See: https://github.com/raspberrypi/rpi-imager/issues/1396
            push(QStringLiteral("          auth:"), netcfg);
            push(QStringLiteral("            key-management: none"), netcfg);
        } else {
            // Required because without proper escaping netplan would fail to parse
            effectiveCryptedPsk.replace('"', QStringLiteral("\\\""));
            // Use password shorthand at access-point level (not inside auth: block)
            // This makes netplan automatically enable WPA2/WPA3 transition mode with PMF optional
            // See: https://github.com/canonical/netplan/blob/main/src/parse.c (handle_access_point_password)
            push(QStringLiteral("          password: \"") + effectiveCryptedPsk + QStringLiteral("\""), netcfg);
        }
        
        push(QStringLiteral("      optional: true"), netcfg);
    }
    
    return netcfg;
}

QByteArray CustomisationGenerator::generateRpiPreseedToml(const QVariantMap& s,
                                                          const QString& piConnectToken,
                                                          bool hasCcRpi,
                                                          bool sshEnabled,
                                                          const QString& currentUser)
{
    Q_UNUSED(hasCcRpi);
    Q_UNUSED(currentUser);

    // Build the section bodies into a single buffer. Each section is only
    // emitted when it has content, and if nothing is configured we return an
    // empty payload so that no rpi-preseed.toml is written to the device.
    QByteArray body;
    auto push = [](const QString& line, QByteArray& out) {
        out += line.toUtf8();
        out += '\n';
    };

    // ---- [system] ----
    const QString hostname = s.value("hostname").toString().trimmed();
    if (!hostname.isEmpty()) {
        push(QStringLiteral("[system]"), body);
        push(QStringLiteral("hostname = ") + tomlQuote(hostname), body);
        push(QString(), body);
    }

    // ---- [user] ----
    // The wizard collects the account name and an already-crypted password hash
    // (yescrypt / sha256crypt). rpi-preseed only touches the account when
    // user.name is present, so emit a name whenever a password is configured.
    const QString userName = s.value("sshUserName").toString().trimmed();
    const QString userPass = s.value("sshUserPassword").toString();
    const bool passwordlessSudo = s.value("passwordlessSudo").toBool();
    if (!userName.isEmpty() || !userPass.isEmpty()) {
        const QString effectiveUser = userName.isEmpty() ? QStringLiteral("pi") : userName;
        push(QStringLiteral("[user]"), body);
        push(QStringLiteral("name = ") + tomlQuote(effectiveUser), body);
        if (!userPass.isEmpty()) {
            push(QStringLiteral("password = ") + tomlQuote(userPass), body);
            push(QStringLiteral("password_encrypted = true"), body);
        }
        // Explicitly make the configured account an admin by adding it to the
        // 'sudo' group. On stock Raspberry Pi OS the renamed UID 1000 user is
        // already a sudoer, so this is a harmless no-op there; the reason we
        // emit it is images (e.g. the CM5 programming jig) whose UID 1000 is
        // deliberately NOT in sudo, where a rename alone would leave the
        // operator unable to escalate. This is intentionally stronger than the
        // cloud-init / firstrun.sh paths, which only preserve existing
        // memberships. rpi-preseed membership is append-only and silently skips
        // the group if the image does not ship it. passwordless_sudo below is
        // the separate, stronger opt-in that also drops a NOPASSWD sudoers rule.
        push(QStringLiteral("groups = [\"sudo\"]"), body);
        if (passwordlessSudo) {
            push(QStringLiteral("passwordless_sudo = true"), body);
        }
        push(QString(), body);
    }

    // ---- [ssh] ----
    // Mirror the cloud-init generator: SSH keys are only meaningful when SSH is
    // enabled, so the whole section is gated on sshEnabled.
    if (sshEnabled) {
        const bool sshPasswordAuth = s.value("sshPasswordAuth").toBool();
        const QString sshPublicKey = s.value("sshPublicKey").toString().trimmed();
        const QString sshAuthorizedKeys = s.value("sshAuthorizedKeys").toString().trimmed();

        QStringList keyList;
        const QString rawKeys = !sshAuthorizedKeys.isEmpty() ? sshAuthorizedKeys : sshPublicKey;
        if (!rawKeys.isEmpty()) {
            const QStringList lines = rawKeys.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
            for (const QString& k : lines) {
                const QString trimmed = k.trimmed();
                if (!trimmed.isEmpty())
                    keyList.append(trimmed);
            }
        }

        push(QStringLiteral("[ssh]"), body);
        push(QStringLiteral("enabled = true"), body);
        push(QStringLiteral("password_authentication = ")
                 + (sshPasswordAuth ? QStringLiteral("true") : QStringLiteral("false")), body);
        if (!keyList.isEmpty()) {
            // Multi-line array of basic strings; rpi-preseed's parser ignores
            // the commas/whitespace/newlines between literals and tolerates the
            // trailing comma.
            push(QStringLiteral("authorized_keys = ["), body);
            for (const QString& k : keyList) {
                push(QStringLiteral("  ") + tomlQuote(k) + QStringLiteral(","), body);
            }
            push(QStringLiteral("]"), body);
        }
        push(QString(), body);
    }

    // ---- [wlan] ----
    const bool wifiConfigured = s.value("wifiConfigured", true).toBool();
    const QByteArray ssidOctets = ssidOctetsFromSettings(s, wifiConfigured);
    if (!ssidOctets.isEmpty()) {
        // TOML strings must be valid UTF-8. A normal SSID goes into wlan.ssid;
        // an exotic non-UTF-8 SSID (arbitrary 802.11 octets) can't be a TOML
        // string, so we hand the raw octets to rpi-preseed as lowercase hex via
        // wlan.ssid_hex, which it decodes and writes as a NetworkManager
        // byte-array SSID.
        const bool ssidIsUtf8 = isValidUtf8(ssidOctets);
        const bool hidden = wifiConfigured
                                ? (s.value("wifiHidden").toBool() || s.value("wifiSSIDHidden").toBool())
                                : false;
        const QString country = s.value("recommendedWifiCountry").toString().trimmed();
        const bool openNetwork = s.value("wifiMode").toString() == QLatin1String("open");

        // Resolve the passphrase/PSK. The wizard normally stores a 64-hex PMK in
        // wifiPasswordCrypt (an encrypted PSK); fall back to the legacy plaintext
        // wifiPassword. rpi-preseed rejects a password on an open network, so we
        // only emit one when the network is not explicitly open.
        QString psk;
        bool pskEncrypted = false;
        if (!openNetwork) {
            psk = wifiConfigured ? s.value("wifiPasswordCrypt").toString() : QString();
            if (!psk.isEmpty()) {
                pskEncrypted = true;
            } else {
                const QString legacy = s.value("wifiPassword").toString();
                if (!legacy.isEmpty()) {
                    psk = legacy;
                    // A 64-hex value is a raw PMK; anything shorter is a passphrase
                    // that rpi-preseed/imager_custom will hash on-device.
                    pskEncrypted = (legacy.length() == 64) && tomlIsHex(legacy);
                }
            }
        }

        push(QStringLiteral("[wlan]"), body);
        if (ssidIsUtf8) {
            push(QStringLiteral("ssid = ") + tomlQuote(QString::fromUtf8(ssidOctets)), body);
        } else {
            push(QStringLiteral("ssid_hex = ") + tomlQuote(QString::fromLatin1(ssidOctets.toHex())), body);
        }
        if (!psk.isEmpty()) {
            push(QStringLiteral("password = ") + tomlQuote(psk), body);
            push(QStringLiteral("password_encrypted = ")
                     + (pskEncrypted ? QStringLiteral("true") : QStringLiteral("false")), body);
        }
        push(QStringLiteral("hidden = ") + (hidden ? QStringLiteral("true") : QStringLiteral("false")), body);
        if (!country.isEmpty()) {
            push(QStringLiteral("country = ") + tomlQuote(country), body);
        }
        // key_mgmt is left implicit: rpi-preseed defaults to wpa-psk when a
        // password is present and none otherwise, which matches the two modes
        // (secure/open) the wizard can produce.
        push(QString(), body);
    }

    // ---- [locale] ----
    const QString timezone = s.value("timezone").toString().trimmed();
    const QString keymap = s.value("keyboard").toString().trimmed();
    if (!timezone.isEmpty() || !keymap.isEmpty()) {
        push(QStringLiteral("[locale]"), body);
        if (!keymap.isEmpty()) {
            push(QStringLiteral("keymap = ") + tomlQuote(keymap), body);
        }
        if (!timezone.isEmpty()) {
            push(QStringLiteral("timezone = ") + tomlQuote(timezone), body);
        }
        push(QString(), body);
    }

    // ---- [connect] ----
    if (s.value("piConnectEnabled").toBool()) {
        const QString token = piConnectToken.trimmed();
        push(QStringLiteral("[connect]"), body);
        push(QStringLiteral("enabled = true"), body);
        if (!token.isEmpty()) {
            push(QStringLiteral("mode = ") + tomlQuote(QStringLiteral("token")), body);
            push(QStringLiteral("token = ") + tomlQuote(token), body);
        } else {
            push(QStringLiteral("mode = ") + tomlQuote(QStringLiteral("device-identity")), body);
        }
        push(QString(), body);
    }

    // ---- [interfaces] ----
    const bool enableI2C = s.value("enableI2C").toBool();
    const bool enableSPI = s.value("enableSPI").toBool();
    const bool enable1Wire = s.value("enable1Wire").toBool();
    const bool enableUsbGadget = s.value("enableUsbGadget").toBool();
    const QString serialRaw = s.value("enableSerial").toString();
    QString serialValue;
    if (serialRaw == QLatin1String("Default"))              serialValue = QStringLiteral("default");
    else if (serialRaw == QLatin1String("Console"))         serialValue = QStringLiteral("console");
    else if (serialRaw == QLatin1String("Hardware"))        serialValue = QStringLiteral("hardware");
    else if (serialRaw == QLatin1String("Console & Hardware")) serialValue = QStringLiteral("console_hardware");
    // "Disabled"/empty: leave serial unset rather than forcing it off.

    if (enableI2C || enableSPI || enable1Wire || enableUsbGadget || !serialValue.isEmpty()) {
        push(QStringLiteral("[interfaces]"), body);
        if (enableI2C)        push(QStringLiteral("i2c = true"), body);
        if (enableSPI)        push(QStringLiteral("spi = true"), body);
        if (enable1Wire)      push(QStringLiteral("onewire = true"), body);
        if (enableUsbGadget)  push(QStringLiteral("usb_gadget = true"), body);
        if (!serialValue.isEmpty())
            push(QStringLiteral("serial = ") + tomlQuote(serialValue), body);
        push(QString(), body);
    }

    if (body.isEmpty())
        return {};

    QByteArray out = "config_version = \"1.0\"\n\n";
    out += body;
    // Collapse the trailing blank line left by the last section.
    while (out.endsWith("\n\n"))
        out.chop(1);
    return out;
}

} // namespace rpi_imager

