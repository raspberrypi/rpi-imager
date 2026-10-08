/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "github_keys.h"

#include <QRegularExpression>

namespace rpi_ssh {

bool isValidGitHubUsername(const QString &username)
{
    static const QRegularExpression re(
        QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9]|-(?=[A-Za-z0-9])){0,38}$"));
    return re.match(username).hasMatch();
}

QUrl gitHubKeysUrl(const QString &username)
{
    return QUrl(QStringLiteral("https://github.com/%1.keys").arg(username));
}

GitHubKeysResult interpretGitHubKeysReply(const QString &username,
                                          const QByteArray &body,
                                          const QString &error)
{
    GitHubKeysResult result;

    if (!error.isEmpty()) {
        static const QRegularExpression status(QStringLiteral("^HTTP (\\d+):"));
        const auto m = status.match(error);
        result.status = (m.hasMatch() && m.captured(1) == QLatin1String("404"))
            ? GitHubKeysStatus::NoSuchUser
            : GitHubKeysStatus::Failed;
        result.detail = error;
        return result;
    }

    // The algorithms OpenSSH accepts in authorized_keys, then a base64 blob.
    // Anything else on a line means this is not the key list.
    static const QRegularExpression keyLine(QStringLiteral(
        "^(ssh-ed25519|ssh-rsa|ssh-dss|ecdsa-sha2-nistp(?:256|384|521)"
        "|sk-ssh-ed25519@openssh\\.com|sk-ecdsa-sha2-nistp256@openssh\\.com)"
        " [A-Za-z0-9+/]+={0,3}$"));

    bool sawText = false;
    const QStringList lines = QString::fromUtf8(body).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        sawText = true;
        if (!keyLine.match(line).hasMatch()) {
            result.status = GitHubKeysStatus::Failed;
            result.keys.clear();
            result.detail = QStringLiteral("reply is not a key list");
            return result;
        }
        const QString key = line + QStringLiteral(" gh:") + username;
        if (!result.keys.contains(key))
            result.keys.append(key);
    }

    result.status = sawText ? GitHubKeysStatus::Keys : GitHubKeysStatus::NoKeys;
    return result;
}

const char *gitHubKeysStatusName(GitHubKeysStatus status)
{
    switch (status) {
    case GitHubKeysStatus::Keys:       return "keys";
    case GitHubKeysStatus::NoKeys:     return "nokeys";
    case GitHubKeysStatus::NoSuchUser: return "nosuchuser";
    case GitHubKeysStatus::Failed:     return "failed";
    }
    return "failed";
}

} // namespace rpi_ssh
