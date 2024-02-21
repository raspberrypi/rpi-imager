/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2023 Raspberry Pi Ltd
 */

#include "macwlancredentials.h"
#include <security/security.h>
#include <QProcess>
#include <QRegularExpression>

QByteArray MacWlanCredentials::getSSID()
{
    /* FIXME: find out the proper API call to get SSID instead of calling command line utilities */
    QString program, regexpstr;
    QStringList args;
    QProcess proc;
    program = "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport";
    args << "-I";
    regexpstr = "[ \t]+SSID: (.+)";

    proc.start(program, args);
    if (proc.waitForStarted(2000) && proc.waitForFinished(2000))
    {
        QRegularExpression rx(regexpstr);
        const QList<QByteArray> outputlines = proc.readAll().replace('\r', "").split('\n');

        for (const QByteArray &line : outputlines) {
            QRegularExpressionMatch match = rx.match(line);
            if (match.hasMatch())
            {
                _ssid = match.captured(1).toLatin1();
                break;
            }
        }
    }

    return _ssid;
}

QByteArray MacWlanCredentials::getPSK()
{
    SecKeychainRef keychainRef;
    QByteArray psk;

    if (_ssid.isEmpty())
        getSSID();
    if (_ssid.isEmpty())
        return psk;

    if (SecKeychainOpen("/Library/Keychains/System.keychain", &keychainRef) == errSecSuccess)
    {
        UInt32 resultLen;
        void *result;
        if (SecKeychainFindGenericPassword(keychainRef, 0, NULL, _ssid.length(), _ssid.constData(), &resultLen, &result, NULL) == errSecSuccess)
        {
            psk = QByteArray((char *) result, resultLen);
            SecKeychainItemFreeContent(NULL, result);
        }
        CFRelease(keychainRef);
    }

    return psk;
}

WlanCredentials *WlanCredentials::_instance = NULL;
WlanCredentials *WlanCredentials::instance()
{
    if (!_instance)
        _instance = new MacWlanCredentials();

    return _instance;
}

