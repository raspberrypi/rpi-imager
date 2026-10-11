/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Saved secrets: sealed at rest on Windows, and kept out of a settings store
 * that belongs to another account.
 */

#include <catch2/catch_test_macros.hpp>

#include "settings_secrets.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

using rpi_imager::SecretSettings;

namespace {

const QString kWifi = QStringLiteral("imagecustomization/wifiPassword");
const QString kHost = QStringLiteral("imagecustomization/hostname");
const QString kOrgKey = QStringLiteral("connect_org_api_key");

struct Store
{
    QTemporaryDir dir;
    QString path = dir.filePath(QStringLiteral("Imager.conf"));
    QSettings settings{path, QSettings::IniFormat};

    QVariant raw(const QString &key) { return settings.value(key); }

    QByteArray onDisk()
    {
        settings.sync();
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
};

} // namespace

TEST_CASE("Secret keys are recognised by name in any group", "[settingssecrets]")
{
    CHECK(SecretSettings::isSecret(kWifi));
    CHECK(SecretSettings::isSecret(QStringLiteral("imagecustomization/sshUserPassword")));
    CHECK(SecretSettings::isSecret(QStringLiteral("imagecustomization/sshAuthorizedKeys")));
    CHECK(SecretSettings::isSecret(kOrgKey));
    CHECK_FALSE(SecretSettings::isSecret(kHost));
    CHECK_FALSE(SecretSettings::isSecret(QStringLiteral("imagecustomization/wifiSSID")));
}

TEST_CASE("A persisted secret reads back and is not stored in the clear",
          "[settingssecrets]")
{
    Store s;
    REQUIRE(s.dir.isValid());
    SecretSettings secrets(s.settings, true);

    secrets.setValue(kWifi, QStringLiteral("hunter2-passphrase"));
    secrets.setValue(kHost, QStringLiteral("pi"));

    CHECK(secrets.value(kWifi).toString() == QStringLiteral("hunter2-passphrase"));
    CHECK(s.raw(kHost).toString() == QStringLiteral("pi"));
#ifdef _WIN32
    CHECK(rpi_imager::isSealed(s.raw(kWifi).toString()));
    CHECK_FALSE(s.onDisk().contains("hunter2-passphrase"));
#else
    CHECK(s.raw(kWifi).toString() == QStringLiteral("hunter2-passphrase"));
#endif

    // A fresh view of the same store, as the next run would have.
    SecretSettings next(s.settings, true);
    CHECK(next.value(kWifi).toString() == QStringLiteral("hunter2-passphrase"));
}

TEST_CASE("A secret saved by an earlier version is sealed in place",
          "[settingssecrets]")
{
    Store s;
    REQUIRE(s.dir.isValid());
    s.settings.setValue(kOrgKey, QStringLiteral("rpak_legacy"));

    SecretSettings secrets(s.settings, true);
    secrets.sealPlaintext();

    CHECK(secrets.value(kOrgKey).toString() == QStringLiteral("rpak_legacy"));
#ifdef _WIN32
    CHECK(rpi_imager::isSealed(s.raw(kOrgKey).toString()));
    CHECK_FALSE(s.onDisk().contains("rpak_legacy"));
#else
    CHECK(s.raw(kOrgKey).toString() == QStringLiteral("rpak_legacy"));
#endif
}

TEST_CASE("Without persistence, another account's secrets are neither read nor replaced",
          "[settingssecrets]")
{
    // Elevated with a shared admin's credentials: what is stored belongs to
    // whoever saved it, and this user must not see it prefilled.
    Store s;
    REQUIRE(s.dir.isValid());
    {
        SecretSettings owner(s.settings, true);
        owner.setValue(kWifi, QStringLiteral("owners-passphrase"));
        owner.setValue(kHost, QStringLiteral("owners-pi"));
    }
    const QVariant ownersStored = s.raw(kWifi);

    SecretSettings secrets(s.settings, false);
    CHECK_FALSE(secrets.persists());
    CHECK_FALSE(secrets.value(kWifi).isValid());
    CHECK_FALSE(secrets.group(QStringLiteral("imagecustomization"))
                    .contains(QStringLiteral("wifiPassword")));

    secrets.setValue(kWifi, QStringLiteral("visitors-passphrase"));
    CHECK(secrets.value(kWifi).toString() == QStringLiteral("visitors-passphrase"));
    CHECK(secrets.group(QStringLiteral("imagecustomization"))
              .value(QStringLiteral("wifiPassword")).toString()
          == QStringLiteral("visitors-passphrase"));
    CHECK(s.raw(kWifi) == ownersStored);
    CHECK_FALSE(s.onDisk().contains("visitors-passphrase"));

    secrets.clearGroup(QStringLiteral("imagecustomization"));
    CHECK_FALSE(secrets.value(kWifi).isValid());
    CHECK_FALSE(s.raw(kHost).isValid());
    CHECK(s.raw(kWifi) == ownersStored);

    secrets.setValue(kOrgKey, QStringLiteral("rpak_visitor"));
    secrets.remove(kOrgKey);
    CHECK_FALSE(secrets.value(kOrgKey).isValid());
}

TEST_CASE("A sealed value that cannot be opened reads as absent", "[settingssecrets]")
{
    // Copied from another profile or machine, or tampered with: handing the
    // ciphertext on would put it on a card as the Wi-Fi passphrase.
    Store s;
    REQUIRE(s.dir.isValid());
    s.settings.setValue(kWifi, QStringLiteral("dpapi:AAAAAAAA"));

    SecretSettings secrets(s.settings, true);
    CHECK_FALSE(secrets.value(kWifi).isValid());
    CHECK_FALSE(secrets.group(QStringLiteral("imagecustomization"))
                    .contains(QStringLiteral("wifiPassword")));
}

#ifdef _WIN32

TEST_CASE("DPAPI sealing round-trips and rejects altered ciphertext",
          "[settingssecrets][windows]")
{
    QString sealed;
    REQUIRE(rpi_imager::sealSecret(QStringLiteral("pässwörd"), &sealed));
    CHECK(rpi_imager::isSealed(sealed));
    CHECK(rpi_imager::unsealSecret(sealed) == QStringLiteral("pässwörd"));

    QByteArray bytes = QByteArray::fromBase64(sealed.mid(6).toLatin1());
    bytes[bytes.size() - 1] = static_cast<char>(bytes.back() ^ 0x01);
    CHECK(rpi_imager::unsealSecret(QStringLiteral("dpapi:") +
                                   QString::fromLatin1(bytes.toBase64()))
              .isNull());

    // Nothing to protect, and an empty ciphertext would not decrypt.
    REQUIRE(rpi_imager::sealSecret(QString(), &sealed));
    CHECK(sealed.isEmpty());
}

#endif // _WIN32
