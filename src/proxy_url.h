// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The proxy URL curl is given, built from what Qt found.
//
// Separated from the detection because the detection cannot be arranged: it
// asks the operating system what proxy is configured, and the answer on a
// build machine is "none". So the part that matters -- which scheme a SOCKS
// proxy gets, and whether credentials are carried over -- had never run,
// while a wrong answer here sends every request to the wrong place or drops
// the credentials that would have let it through.

#ifndef RPI_IMAGER_PROXY_URL_H
#define RPI_IMAGER_PROXY_URL_H

#include <QByteArray>
#include <QNetworkProxy>
#include <QUrl>

namespace rpi_imager {

// Empty where the proxy is not one to use: no proxy at all, or one with no
// host to send anything to.
inline QByteArray proxyUrlFor(const QNetworkProxy &proxy)
{
    if (proxy.type() == QNetworkProxy::NoProxy)
        return {};
    if (proxy.hostName().isEmpty())
        return {};

    QUrl url;
    // socks5h rather than socks5: the h asks curl to resolve the name at the
    // proxy rather than here. Resolving locally leaks every hostname to the
    // local resolver, and fails outright where the name only exists on the
    // far side.
    url.setScheme(proxy.type() == QNetworkProxy::Socks5Proxy
                      ? QStringLiteral("socks5h")
                      : QStringLiteral("http"));
    url.setHost(proxy.hostName());
    if (proxy.port() != 0)
        url.setPort(proxy.port());

    if (!proxy.user().isEmpty()) {
        url.setUserName(proxy.user());
        url.setPassword(proxy.password());
    }

    return url.toEncoded();
}

} // namespace rpi_imager

#endif // RPI_IMAGER_PROXY_URL_H
