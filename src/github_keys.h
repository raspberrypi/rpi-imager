/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * SSH public keys from a GitHub account, as ssh-import-id gh:<user> gets them.
 *
 * Embedded Imager runs on a Pi with no clipboard and no file browser, so the
 * paste field and the Browse button cannot bring a key in. A GitHub username
 * can be typed: https://github.com/<user>.keys serves that account's public
 * keys, one per line.
 */

#ifndef GITHUB_KEYS_H
#define GITHUB_KEYS_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace rpi_ssh {

// GitHub's rule: 1 to 39 characters, ASCII letters, digits and single
// hyphens, not starting or ending with one. Checked before anything is
// fetched, so a typo is answered at once and nothing user-typed is put into
// a URL unchecked.
bool isValidGitHubUsername(const QString &username);

QUrl gitHubKeysUrl(const QString &username);

enum class GitHubKeysStatus {
    Keys,        // at least one key
    NoKeys,      // the account exists and has published none
    NoSuchUser,  // GitHub answered 404
    Failed,      // no answer, or an answer that is not a key list
};

struct GitHubKeysResult {
    GitHubKeysStatus status = GitHubKeysStatus::Failed;
    QStringList keys;
    QString detail;  // why it failed, for the log
};

// What a fetch of gitHubKeysUrl() came to. `error` is CurlFetcher's message,
// empty on success; it reports a status as "HTTP <code>: ...".
//
// Each key gets "gh:<username>" as its comment, so the list shows where it
// came from -- GitHub serves them with none. A 200 that holds no key at all
// is Failed rather than NoKeys unless it is empty: a captive portal's login
// page is not an account with no keys.
GitHubKeysResult interpretGitHubKeysReply(const QString &username,
                                          const QByteArray &body,
                                          const QString &error);

const char *gitHubKeysStatusName(GitHubKeysStatus status);

} // namespace rpi_ssh

#endif // GITHUB_KEYS_H
