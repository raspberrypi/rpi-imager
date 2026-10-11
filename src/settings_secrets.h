/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#ifndef SETTINGS_SECRETS_H
#define SETTINGS_SECRETS_H

#include <QString>
#include <QVariant>
#include <QVariantMap>

class QSettings;

namespace rpi_imager {

/*
 * Whether saved secrets may be written to, and read from, this account's
 * settings. False on Windows when Imager was elevated with another account's
 * credentials: that account's hive is then shared by everyone who elevates
 * with it.
 */
bool secretsMayPersist();

/*
 * DPAPI on Windows, bound to the current account; elsewhere the value is
 * returned unchanged. unsealSecret() returns a null QString when a sealed
 * value cannot be opened.
 */
bool sealSecret(const QString& plain, QString* sealed);
QString unsealSecret(const QString& stored);
bool isSealed(const QString& stored);

/*
 * The secret keys of a QSettings, sealed at rest. When they may not persist,
 * they live only in this object, and what is stored is neither read nor
 * overwritten.
 */
class SecretSettings
{
public:
    SecretSettings(QSettings& settings, bool persist);

    static bool isSecret(const QString& key);

    bool persists() const { return _persist; }

    void setValue(const QString& key, const QVariant& value);
    QVariant value(const QString& key) const;
    void remove(const QString& key);

    // Every key under group, secrets included.
    QVariantMap group(const QString& group) const;
    void clearGroup(const QString& group);

    // Seals secrets an earlier version stored in the clear.
    void sealPlaintext();

private:
    QSettings& _settings;
    const bool _persist;
    QVariantMap _session;
};

} // namespace rpi_imager

#endif // SETTINGS_SECRETS_H
