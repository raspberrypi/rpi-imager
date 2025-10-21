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

    /* Removed legacy 'airport' tool fallback */

    /* Removed keychain-based SSID inference to avoid mis-filling */

    return _ssid;
}

QByteArray MacWlanCredentials::getPSK()
{
    if (_ssid.isEmpty())
    {
        qDebug() << "MacWlanCredentials::getPSK(): _ssid is empty, calling getSSID()";
        getSSID();
    }
    if (_ssid.isEmpty())
    {
        qDebug() << "MacWlanCredentials::getPSK(): _ssid still empty after getSSID(), cannot retrieve PSK";
        return QByteArray();
    }
    
    return getPSKForSSID(_ssid);
}

QByteArray MacWlanCredentials::getPSKForSSID(const QByteArray &ssid)
{
    if (ssid.isEmpty())
    {
        qDebug() << "MacWlanCredentials::getPSKForSSID(): Empty SSID provided, cannot retrieve PSK";
        return QByteArray();
    }
    
    qDebug() << "MacWlanCredentials::getPSKForSSID(): Attempting to retrieve PSK for SSID:" << ssid;

    SecKeychainRef keychainRef;
    QByteArray psk;

    auto fetchLatestForSsid = [&ssid](SecKeychainRef kc) -> QByteArray {
        QByteArray bestPsk;
        QByteArray bestModDate;
        const char serviceName[] = "AirPort";
        const UInt32 serviceLen = sizeof(serviceName) - 1;

        SecKeychainSearchRef search = NULL;
        SecKeychainItemRef item = NULL;

        SecKeychainAttribute attrs[2];
        attrs[0].tag = kSecServiceItemAttr; attrs[0].length = serviceLen; attrs[0].data = (void*)serviceName;
        attrs[1].tag = kSecAccountItemAttr; attrs[1].length = ssid.length(); attrs[1].data = (void*)ssid.constData();
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
                                           ssid.length(), ssid.constData(),
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
                                           ssid.length(), ssid.constData(),
                                           &resultLen, &result, NULL) == errSecSuccess)
        {
            psk = QByteArray((char *) result, resultLen);
            SecKeychainItemFreeContent(NULL, result);
        }
    }

    if (!psk.isEmpty())
    {
        qDebug() << "MacWlanCredentials::getPSKForSSID(): Successfully retrieved PSK for SSID:" << ssid;
    }
    else
    {
        qDebug() << "MacWlanCredentials::getPSKForSSID(): No PSK found in keychain for SSID:" << ssid;
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

