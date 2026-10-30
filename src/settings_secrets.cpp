/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "settings_secrets.h"

#include <QByteArray>
#include <QDebug>
#include <QSettings>
#include <QStringList>

#ifdef Q_OS_WIN
#include <string>
#include <vector>
#include <windows.h>
#include <wincrypt.h>
#include <wtsapi32.h>
#endif

namespace rpi_imager {

namespace {

const QLatin1String kSealedPrefix("dpapi:");

} // namespace

bool isSealed(const QString& stored)
{
    return stored.startsWith(kSealedPrefix);
}

#ifdef Q_OS_WIN

namespace {

const QByteArray kEntropy("Raspberry Pi Imager settings");

DATA_BLOB blobOf(const QByteArray& bytes)
{
    return DATA_BLOB{static_cast<DWORD>(bytes.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))};
}

std::vector<BYTE> processUser()
{
    std::vector<BYTE> user;
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return user;
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    user.resize(size);
    if (size == 0 || !::GetTokenInformation(token, TokenUser, user.data(), size, &size))
        user.clear();
    ::CloseHandle(token);
    return user;
}

std::wstring sessionAccount()
{
    LPWSTR name = nullptr;
    LPWSTR domain = nullptr;
    DWORD bytes = 0;
    std::wstring account;
    if (::WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
                                      WTSUserName, &name, &bytes) &&
        ::WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
                                      WTSDomainName, &domain, &bytes) &&
        name && *name) {
        account = (domain && *domain) ? std::wstring(domain) + L"\\" + name : std::wstring(name);
    }
    if (name)
        ::WTSFreeMemory(name);
    if (domain)
        ::WTSFreeMemory(domain);
    return account;
}

} // namespace

// The leak needs a UAC prompt on somebody's desktop. With nobody signed in
// (a service, SSH, CI) there is no one to share with; a signed-in account
// that cannot be resolved fails closed.
bool secretsMayPersist()
{
    const std::vector<BYTE> user = processUser();
    if (user.empty())
        return false;
    const std::wstring account = sessionAccount();
    if (account.empty())
        return true;

    DWORD sidSize = 0;
    DWORD domainSize = 0;
    SID_NAME_USE use;
    ::LookupAccountNameW(nullptr, account.c_str(), nullptr, &sidSize, nullptr, &domainSize, &use);
    if (sidSize == 0)
        return false;
    std::vector<BYTE> sid(sidSize);
    std::vector<wchar_t> domain(domainSize + 1);
    if (!::LookupAccountNameW(nullptr, account.c_str(), sid.data(), &sidSize, domain.data(),
                              &domainSize, &use))
        return false;
    return ::EqualSid(sid.data(), reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid);
}

bool sealSecret(const QString& plain, QString* sealed)
{
    if (plain.isEmpty()) {
        *sealed = plain;
        return true;
    }
    const QByteArray bytes = plain.toUtf8();
    DATA_BLOB in = blobOf(bytes);
    DATA_BLOB entropy = blobOf(kEntropy);
    DATA_BLOB out{};
    if (!::CryptProtectData(&in, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    *sealed = kSealedPrefix +
              QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(out.pbData),
                                             static_cast<qsizetype>(out.cbData))
                                      .toBase64());
    ::LocalFree(out.pbData);
    return true;
}

QString unsealSecret(const QString& stored)
{
    if (!isSealed(stored))
        return stored;
    const auto decoded = QByteArray::fromBase64Encoding(
        stored.mid(kSealedPrefix.size()).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty())
        return {};
    DATA_BLOB in = blobOf(decoded.decoded);
    DATA_BLOB entropy = blobOf(kEntropy);
    DATA_BLOB out{};
    if (!::CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    QString plain = QString::fromUtf8(reinterpret_cast<const char*>(out.pbData),
                                      static_cast<qsizetype>(out.cbData));
    ::SecureZeroMemory(out.pbData, out.cbData);
    ::LocalFree(out.pbData);
    return plain;
}

#else

bool secretsMayPersist()
{
    return true;
}

bool sealSecret(const QString& plain, QString* sealed)
{
    *sealed = plain;
    return true;
}

// Sealed elsewhere, by an account this platform cannot speak for.
QString unsealSecret(const QString& stored)
{
    return isSealed(stored) ? QString() : stored;
}

#endif

SecretSettings::SecretSettings(QSettings& settings, bool persist)
    : _settings(settings), _persist(persist)
{
}

bool SecretSettings::isSecret(const QString& key)
{
    static const QStringList secrets{
        QStringLiteral("wifiPassword"),
        QStringLiteral("wifiPasswordCrypt"),
        QStringLiteral("sshUserPassword"),
        QStringLiteral("sshAuthorizedKeys"),
        QStringLiteral("connect_org_api_key"),
    };
    return secrets.contains(key.section(QLatin1Char('/'), -1));
}

void SecretSettings::setValue(const QString& key, const QVariant& value)
{
    if (!isSecret(key)) {
        _settings.setValue(key, value);
        return;
    }
    if (!_persist) {
        _session.insert(key, value);
        return;
    }
    QString sealed;
    if (sealSecret(value.toString(), &sealed)) {
        _session.remove(key);
        _settings.setValue(key, sealed);
    } else {
        qWarning() << "Could not encrypt a saved secret; it is kept for this session only";
        _session.insert(key, value);
        _settings.remove(key);
    }
}

QVariant SecretSettings::value(const QString& key) const
{
    if (!isSecret(key))
        return _settings.value(key);
    if (_session.contains(key))
        return _session.value(key);
    if (!_persist)
        return {};

    const QVariant stored = _settings.value(key);
    if (!isSealed(stored.toString()))
        return stored;
    const QString plain = unsealSecret(stored.toString());
    if (plain.isNull())
        return {};
    return plain;
}

void SecretSettings::remove(const QString& key)
{
    if (isSecret(key)) {
        _session.remove(key);
        if (!_persist)
            return;
    }
    _settings.remove(key);
}

QVariantMap SecretSettings::group(const QString& group) const
{
    const QString prefix = group + QLatin1Char('/');
    QStringList keys;
    _settings.beginGroup(group);
    keys = _settings.childKeys();
    _settings.endGroup();
    for (auto it = _session.constBegin(); it != _session.constEnd(); ++it) {
        const QString rest = it.key().mid(prefix.size());
        if (it.key().startsWith(prefix) && !rest.contains(QLatin1Char('/')) &&
            !keys.contains(rest))
            keys.append(rest);
    }

    QVariantMap result;
    for (const QString& key : keys) {
        const QVariant v = value(prefix + key);
        if (v.isValid())
            result.insert(key, v);
    }
    return result;
}

void SecretSettings::clearGroup(const QString& group)
{
    const QString prefix = group + QLatin1Char('/');
    for (auto it = _session.begin(); it != _session.end();) {
        if (it.key().startsWith(prefix))
            it = _session.erase(it);
        else
            ++it;
    }

    if (_persist) {
        _settings.remove(group);
        return;
    }
    _settings.beginGroup(group);
    const QStringList keys = _settings.childKeys();
    for (const QString& key : keys) {
        if (!isSecret(key))
            _settings.remove(key);
    }
    _settings.endGroup();
}

void SecretSettings::sealPlaintext()
{
    if (!_persist)
        return;
    bool changed = false;
    const QStringList keys = _settings.allKeys();
    for (const QString& key : keys) {
        if (!isSecret(key))
            continue;
        const QString stored = _settings.value(key).toString();
        QString sealed;
        if (isSealed(stored) || !sealSecret(stored, &sealed) || sealed == stored)
            continue;
        _settings.setValue(key, sealed);
        changed = true;
    }
    if (changed)
        _settings.sync();
}

} // namespace rpi_imager
