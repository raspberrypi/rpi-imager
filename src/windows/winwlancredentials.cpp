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

#include "wlan_profile_xml.h"
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


WinWlanCredentials::~WinWlanCredentials()
{
    // The passphrase is wiped where it lives, which is the whole point: it
    // must not survive in a core dump, in swap, or in memory handed back to
    // the allocator and read by whatever gets it next.
    _psk.wipe();

    // The SSID is not a secret and is not shared with anyone by the time we
    // get here, so const_cast is enough to clear it without detaching into a
    // fresh copy and zeroing that instead.
    if (!_ssid.isEmpty()) {
        rpi_imager::secureZero(const_cast<char *>(_ssid.constData()),
                               static_cast<size_t>(_ssid.size()));
        _ssid.clear();
    }
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
                            QByteArray psk = rpi_wlan::pskFromProfileXml(xml);
                            _psk.assign(psk);
                            // The intermediate goes too: it held the same
                            // passphrase, and it is ours alone to clear.
                            rpi_imager::secureZero(psk.data(), psk.size());
                            psk.clear();

                            // Zero the local XML string that contains the plaintext PSK
                            // inside <keyMaterial> tags before it goes out of scope.
                            if (xml.size() > 0) {
                                SecureZeroMemory(xml.data(), xml.size() * sizeof(QChar));
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
    // A copy, sharing nothing with our storage -- so wiping ours later
    // cannot reach into the caller's, and the caller cannot keep ours alive.
    return _psk.copy();
}

QByteArray WinWlanCredentials::getPSKForSSID(const QByteArray &ssid)
{
    // Windows implementation caches both SSID and PSK during construction
    // If requested SSID matches cached SSID, return cached PSK
    if (ssid == _ssid && !_psk.empty()) {
        return _psk.copy();
    }
    // Otherwise, return empty (would need to re-query Windows WLAN API)
    return QByteArray();
}

WlanCredentials *WlanCredentials::_instance = NULL;
WlanCredentials *WlanCredentials::instance()
{
    if (!_instance)
        _instance = new WinWlanCredentials();

    return _instance;
}
