/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#include <windows.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <string>
#include <algorithm>
#include <cctype>

namespace PlatformQuirks {

static bool hasNvidiaGraphicsCard() {
    HRESULT hres;
    
    // Initialize COM
    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) {
        return false;
    }

    // Set general COM security levels
    hres = CoInitializeSecurity(
        NULL,
        -1,                          // COM authentication
        NULL,                        // Authentication services
        NULL,                        // Reserved
        RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication
        RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation
        NULL,                        // Authentication info
        EOAC_NONE,                   // Additional capabilities
        NULL                         // Reserved
    );

    if (FAILED(hres)) {
        CoUninitialize();
        return false;
    }

    // Obtain the initial locator to WMI
    IWbemLocator *pLoc = NULL;
    hres = CoCreateInstance(
        CLSID_WbemLocator,
        0,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID *) &pLoc);

    if (FAILED(hres)) {
        CoUninitialize();
        return false;
    }

    // Connect to WMI through the IWbemLocator::ConnectServer method
    IWbemServices *pSvc = NULL;
    BSTR wmiNamespace = SysAllocString(L"ROOT\\CIMV2");
    hres = pLoc->ConnectServer(
        wmiNamespace,            // Object path of WMI namespace
        NULL,                    // User name. NULL = current user
        NULL,                    // User password. NULL = current
        0,                       // Locale. NULL indicates current
        0,                       // Security flags.
        0,                       // Authority (for NTLM)
        0,                       // Context object
        &pSvc                    // pointer to IWbemServices proxy
    );
    SysFreeString(wmiNamespace);

    if (FAILED(hres)) {
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    // Set security levels on the proxy
    hres = CoSetProxyBlanket(
        pSvc,                        // Indicates the proxy to set security on
        RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx
        RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx
        NULL,                        // Server principal name
        RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx
        RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
        NULL,                        // client identity
        EOAC_NONE                    // proxy capabilities
    );

    if (FAILED(hres)) {
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    // Use the IWbemServices pointer to make requests of WMI
    IEnumWbemClassObject* pEnumerator = NULL;
    BSTR wqlLanguage = SysAllocString(L"WQL");
    BSTR wqlQuery = SysAllocString(L"SELECT Name FROM Win32_VideoController");
    hres = pSvc->ExecQuery(
        wqlLanguage,
        wqlQuery,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &pEnumerator);
    SysFreeString(wqlLanguage);
    SysFreeString(wqlQuery);

    if (FAILED(hres)) {
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    // Check the query results for NVIDIA
    IWbemClassObject *pclsObj = NULL;
    ULONG uReturn = 0;
    bool foundNvidia = false;

    while (pEnumerator) {
        HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

        if (0 == uReturn) {
            break;
        }

        VARIANT vtProp;
        hr = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
        if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR) {
            // Convert BSTR to std::string
            int len = WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, nullptr, 0, nullptr, nullptr);
            std::string deviceName(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, &deviceName[0], len, nullptr, nullptr);
            
            // Convert to lowercase for case-insensitive comparison
            std::string lowerDeviceName = deviceName;
            std::transform(lowerDeviceName.begin(), lowerDeviceName.end(), lowerDeviceName.begin(), ::tolower);
            
            // Check if this is an NVIDIA device (case-insensitive)
            if (lowerDeviceName.find("nvidia") != std::string::npos || 
                lowerDeviceName.find("geforce") != std::string::npos ||
                lowerDeviceName.find("quadro") != std::string::npos ||
                lowerDeviceName.find("tesla") != std::string::npos ||
                lowerDeviceName.find("rtx") != std::string::npos ||
                lowerDeviceName.find("gtx") != std::string::npos) {
                foundNvidia = true;
            }
        }
        VariantClear(&vtProp);
        pclsObj->Release();
    }

    // Cleanup
    pSvc->Release();
    pLoc->Release();
    pEnumerator->Release();
    CoUninitialize();

    return foundNvidia;
}

void applyQuirks() {
    // Check for NVIDIA graphics cards and apply software renderer workaround
    if (hasNvidiaGraphicsCard()) {
        SetEnvironmentVariableA("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
    }
}

void beep() {
    // Use Windows MessageBeep for system beep sound
    MessageBeep(MB_OK);
}

} // namespace PlatformQuirks