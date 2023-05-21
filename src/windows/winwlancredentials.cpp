/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2023 Raspberry Pi Ltd
 */

#include "winwlancredentials.h"
#include <windows.h>
#include <winioctl.h>
#include <wlanapi.h>
#include <Windot11.h>
#include <delayimp.h>
#include <QDebug>
#include <QRegularExpression>
#ifndef WLAN_PROFILE_GET_PLAINTEXT_KEY
#define WLAN_PROFILE_GET_PLAINTEXT_KEY 4
#endif

static HINSTANCE hWlanApi = NULL;

/* Called by dlltool generated delaylib code that is used for lazy loading
   wlanapi.dll */
FARPROC WINAPI dllDelayNotifyHook(unsigned dliNotify, PDelayLoadInfo)
{
    if (dliNotify == dliNotePreLoadLibrary)
    {
        return (FARPROC) hWlanApi;
    }

    return NULL;
}

PfnDliHook __pfnDliNotifyHook2 = dllDelayNotifyHook;

inline QString unescapeXml(QString str)
{
    static const char *table[] = {
        "&lt;", "<",
        "&gt;", ">",
        "&quot;", "\"",
        "&apos;", "'",
        "&amp;", "&"
    };
    int tableLen = sizeof(table) / sizeof(table[0]);

    for (int i=0; i < tableLen; i+=2)
    {
        str.replace(table[i], table[i+1]);
    }

    return str;
}

WinWlanCredentials::WinWlanCredentials()
{
    if (!hWlanApi)
        hWlanApi = LoadLibraryExA("wlanapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hWlanApi)
    {
        qDebug() << "wlanapi.dll not available";
        return;
    }

    /* Get both SSID and PSK in one go, and store it in variables */
    HANDLE h;
    DWORD supportedVersion = 0;
    DWORD clientVersion = 2;

    if (WlanOpenHandle(clientVersion, NULL, &supportedVersion, &h) != ERROR_SUCCESS)
        return;

    PWLAN_INTERFACE_INFO_LIST ifList = NULL;

    if (WlanEnumInterfaces(h, NULL, &ifList) == ERROR_SUCCESS)
    {
        for (DWORD i=0; i < ifList->dwNumberOfItems; i++)
        {
            PWLAN_PROFILE_INFO_LIST profileList = NULL;

            if (ifList->InterfaceInfo[i].isState != wlan_interface_state_connected)
                continue; /* Wlan adapter not connected */

            /* Get current connection info for SSID */
            PWLAN_CONNECTION_ATTRIBUTES pConnectInfo = NULL;
            DWORD connectInfoSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
            WLAN_OPCODE_VALUE_TYPE opCode = wlan_opcode_value_type_invalid;

            if (WlanQueryInterface(h, &ifList->InterfaceInfo[i].InterfaceGuid,
                               wlan_intf_opcode_current_connection, NULL,
                               &connectInfoSize, (PVOID *) &pConnectInfo, &opCode) != ERROR_SUCCESS || !pConnectInfo)
            {
                continue;
            }

            if (pConnectInfo->wlanAssociationAttributes.dot11Ssid.uSSIDLength)
            {
                _ssid = QByteArray((const char *) pConnectInfo->wlanAssociationAttributes.dot11Ssid.ucSSID,
                                   pConnectInfo->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
            }
            WlanFreeMemory(pConnectInfo);

            if (_ssid.isEmpty())
                continue;

            if (WlanGetProfileList(h, &ifList->InterfaceInfo[i].InterfaceGuid,
                                   NULL, &profileList) == ERROR_SUCCESS)
            {
                for (DWORD j=0; j < profileList->dwNumberOfItems; j++)
                {
                    QString s = QString::fromWCharArray(profileList->ProfileInfo[j].strProfileName);
                    qDebug() << "Enumerating wlan profiles, SSID found:" << s << " looking for:" << _ssid;

                    if (s == _ssid) {
                        DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
                        DWORD access = 0;
                        DWORD ret = 0;
                        LPWSTR xmlstr = NULL;

                        if ( (ret = WlanGetProfile(h, &ifList->InterfaceInfo[i].InterfaceGuid, profileList->ProfileInfo[j].strProfileName,
                                          NULL, &xmlstr, &flags, &access)) == ERROR_SUCCESS && xmlstr)
                        {
                            QString xml = QString::fromWCharArray(xmlstr);
                            qDebug() << "XML wlan profile:" << xml;
                            QRegularExpression rx("<keyMaterial>(.+)</keyMaterial>");
                            QRegularExpressionMatch match = rx.match(xml);

                            if (match.hasMatch()) {
                                _psk = unescapeXml(match.captured(1)).toLatin1();
                            }

                            WlanFreeMemory(xmlstr);
                            break;
                        }
                    }
                }
            }

            if (profileList) {
                WlanFreeMemory(profileList);
            }
        }
    }

    if (ifList)
        WlanFreeMemory(ifList);
    WlanCloseHandle(h, NULL);
}

QByteArray WinWlanCredentials::getSSID()
{
    return _ssid;
}

QByteArray WinWlanCredentials::getPSK()
{
    return _psk;
}

WlanCredentials *WlanCredentials::_instance = NULL;
WlanCredentials *WlanCredentials::instance()
{
    if (!_instance)
        _instance = new WinWlanCredentials();

    return _instance;
}
