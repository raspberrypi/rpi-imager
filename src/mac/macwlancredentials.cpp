/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2023 Raspberry Pi Ltd
 */

#include "macwlancredentials.h"
#include <Security/Security.h>
#include <QProcess>
#include <QRegularExpression>
#include "ssid_helper.h"
#include "location_helper.h"

QByteArray MacWlanCredentials::getSSID()
{
    /* Request location permission (needed for SSID access on newer macOS) */
    rpiimager_request_location_permission();

    /* Prefer CoreWLAN via Objective-C++ helper */
    if (_ssid.isEmpty())
    {
        const char *ssid_c = rpiimager_current_ssid_cstr();
        if (ssid_c)
        {
            _ssid = QByteArray(ssid_c);
            free((void*)ssid_c);
            if (!_ssid.isEmpty())
                qDebug() << "Detected SSID via CoreWLAN:" << _ssid;
        }
    }

    /* Legacy fallback: try external airport tool */
    if (_ssid.isEmpty())
    {
        QString program, regexpstr;
        QStringList args;
        QProcess proc;
        program = "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport";
        args << "-I";
        regexpstr = "[ \t]+SSID: (.+)";

        proc.start(program, args);
        if (proc.waitForStarted(1000) && proc.waitForFinished(1500))
        {
            QRegularExpression rx(regexpstr);
            const QList<QByteArray> outputlines = proc.readAll().replace('\r', "").split('\n');

            for (const QByteArray &line : outputlines) {
                QRegularExpressionMatch match = rx.match(line);
                if (match.hasMatch())
                {
                    _ssid = match.captured(1).toLatin1();
                    if (!_ssid.isEmpty())
                        qDebug() << "Detected SSID via airport:" << _ssid;
                    break;
                }
            }
        }
    }

    /* Fallback: enumerate keychain for latest AirPort network and use its account (SSID) */
    if (_ssid.isEmpty())
    {
        auto fetchLatestSsid = []() -> QByteArray {
            QByteArray bestSsid;
            QByteArray bestModDate;
            const char serviceName[] = "AirPort";
            const UInt32 serviceLen = sizeof(serviceName) - 1;

            auto scan = [&](SecKeychainRef kc) {
                SecKeychainSearchRef search = NULL;
                SecKeychainItemRef item = NULL;

                SecKeychainAttribute attrs[1];
                attrs[0].tag = kSecServiceItemAttr; attrs[0].length = serviceLen; attrs[0].data = (void*)serviceName;
                SecKeychainAttributeList attrList = { 1, attrs };

                if (SecKeychainSearchCreateFromAttributes(kc, kSecGenericPasswordItemClass, &attrList, &search) == errSecSuccess && search)
                {
                    while (SecKeychainSearchCopyNext(search, &item) == errSecSuccess && item)
                    {
                        SecKeychainAttributeList *outAttrs = NULL;
                        if (SecKeychainItemCopyAttributesAndData(item, NULL, NULL, &outAttrs, NULL, NULL) == errSecSuccess)
                        {
                            QByteArray modDateStr;
                            QByteArray accountStr;
                            if (outAttrs)
                            {
                                for (UInt32 i = 0; i < outAttrs->count; ++i)
                                {
                                    const SecKeychainAttribute &a = outAttrs->attr[i];
                                    if (a.tag == kSecModDateItemAttr && a.length > 0 && a.data)
                                        modDateStr = QByteArray(static_cast<const char*>(a.data), a.length);
                                    else if (a.tag == kSecAccountItemAttr && a.length > 0 && a.data)
                                        accountStr = QByteArray(static_cast<const char*>(a.data), a.length);
                                }
                            }

                            if (!accountStr.isEmpty() && (bestModDate.isEmpty() || (!modDateStr.isEmpty() && modDateStr > bestModDate)))
                            {
                                bestModDate = modDateStr;
                                bestSsid = accountStr;
                            }
                            SecKeychainItemFreeAttributesAndData(outAttrs, NULL);
                        }

                        CFRelease(item);
                        item = NULL;
                    }
                    CFRelease(search);
                }
            };

            /* Prefer System keychain first */
            SecKeychainRef kc = NULL;
            if (SecKeychainOpen("/Library/Keychains/System.keychain", &kc) == errSecSuccess)
            {
                scan(kc);
                CFRelease(kc);
            }
            /* Then user's default (login) keychain */
            if (bestSsid.isEmpty() && SecKeychainCopyDefault(&kc) == errSecSuccess)
            {
                scan(kc);
                CFRelease(kc);
            }
            /* Finally, search the whole keychain search list */
            if (bestSsid.isEmpty())
            {
                scan(NULL);
            }

            return bestSsid;
        };

        QByteArray candidate = fetchLatestSsid();
        if (!candidate.isEmpty())
        {
            _ssid = candidate;
            qDebug() << "Detected SSID from Keychain:" << _ssid;
        }
        // Do not infer SSID from keychain; avoid mis-filling when not associated
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

    auto fetchLatestForSsid = [this](SecKeychainRef kc) -> QByteArray {
        QByteArray bestPsk;
        QByteArray bestModDate;
        const char serviceName[] = "AirPort";
        const UInt32 serviceLen = sizeof(serviceName) - 1;

        SecKeychainSearchRef search = NULL;
        SecKeychainItemRef item = NULL;

        SecKeychainAttribute attrs[2];
        attrs[0].tag = kSecServiceItemAttr; attrs[0].length = serviceLen; attrs[0].data = (void*)serviceName;
        attrs[1].tag = kSecAccountItemAttr; attrs[1].length = _ssid.length(); attrs[1].data = (void*)_ssid.constData();
        SecKeychainAttributeList attrList = { 2, attrs };

        if (SecKeychainSearchCreateFromAttributes(kc, kSecGenericPasswordItemClass, &attrList, &search) == errSecSuccess && search)
        {
            while (SecKeychainSearchCopyNext(search, &item) == errSecSuccess && item)
            {
                SecKeychainAttributeList *outAttrs = NULL;
                UInt32 pwdLen = 0; void *pwdData = NULL;
                if (SecKeychainItemCopyAttributesAndData(item, NULL, NULL, &outAttrs, &pwdLen, &pwdData) == errSecSuccess)
                {
                    QByteArray modDateStr;
                    if (outAttrs)
                    {
                        for (UInt32 i = 0; i < outAttrs->count; ++i)
                        {
                            const SecKeychainAttribute &a = outAttrs->attr[i];
                            if (a.tag == kSecModDateItemAttr && a.length > 0 && a.data)
                            {
                                modDateStr = QByteArray(static_cast<const char*>(a.data), a.length);
                                break;
                            }
                        }
                    }

                    // Choose the lexicographically greatest mod date (format is YYYYMMDDhhmmssZ)
                    if (bestModDate.isEmpty() || (!modDateStr.isEmpty() && modDateStr > bestModDate))
                    {
                        bestModDate = modDateStr;
                        bestPsk = QByteArray(static_cast<const char*>(pwdData), pwdLen);
                    }

                    SecKeychainItemFreeAttributesAndData(outAttrs, pwdData);
                }

                CFRelease(item);
                item = NULL;
            }
            CFRelease(search);
        }

        return bestPsk;
    };

    /* First, explicitly search the System keychain as many WiFi passwords are stored there */
    if (SecKeychainOpen("/Library/Keychains/System.keychain", &keychainRef) == errSecSuccess)
    {
        psk = fetchLatestForSsid(keychainRef);
        CFRelease(keychainRef);
    }

    /* If not found in System keychain, prefer user's default keychain (login) */
    /* Prefer user's default keychain; WiFi passwords are stored in login keychain under service "AirPort" */
    if (psk.isEmpty() && SecKeychainCopyDefault(&keychainRef) == errSecSuccess)
    {
        psk = fetchLatestForSsid(keychainRef);
        CFRelease(keychainRef);
    }

    /* Fallback: search default keychain search list with explicit service */
    if (psk.isEmpty())
    {
        UInt32 resultLen = 0;
        void *result = NULL;
        const char serviceName[] = "AirPort";
        const UInt32 serviceLen = sizeof(serviceName) - 1;
        if (SecKeychainFindGenericPassword(NULL,
                                           serviceLen, serviceName,
                                           _ssid.length(), _ssid.constData(),
                                           &resultLen, &result, NULL) == errSecSuccess)
        {
            psk = QByteArray((char *) result, resultLen);
            SecKeychainItemFreeContent(NULL, result);
        }
    }

    /* Final fallback: search by account only (may match multiple, but better than nothing) */
    if (psk.isEmpty())
    {
        UInt32 resultLen = 0;
        void *result = NULL;
        if (SecKeychainFindGenericPassword(NULL,
                                           0, NULL,
                                           _ssid.length(), _ssid.constData(),
                                           &resultLen, &result, NULL) == errSecSuccess)
        {
            psk = QByteArray((char *) result, resultLen);
            SecKeychainItemFreeContent(NULL, result);
        }
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

