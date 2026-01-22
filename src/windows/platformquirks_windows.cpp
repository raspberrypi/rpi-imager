/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // Windows 10 or later
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <iphlpapi.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <QProcess>
#include <QDebug>

namespace {
    // Network monitoring state
    HANDLE g_notificationHandle = NULL;
    PlatformQuirks::NetworkStatusCallback g_networkCallback = nullptr;
    CRITICAL_SECTION g_callbackLock;
    INIT_ONCE g_callbackLockInitOnce = INIT_ONCE_STATIC_INIT;
    
    BOOL CALLBACK initCallbackLockOnce(PINIT_ONCE initOnce, PVOID param, PVOID* context) {
        (void)initOnce;
        (void)param;
        (void)context;
        InitializeCriticalSection(&g_callbackLock);
        return TRUE;
    }
    
    void initCallbackLock() {
        InitOnceExecuteOnce(&g_callbackLockInitOnce, initCallbackLockOnce, nullptr, nullptr);
    }
    
    void NETIOAPI_API_ networkChangeCallback(PVOID callerContext, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE notificationType) {
        (void)callerContext;
        (void)row;
        (void)notificationType;
        
        // Ensure lock is initialized (safe to call multiple times)
        initCallbackLock();
        
        EnterCriticalSection(&g_callbackLock);
        if (g_networkCallback) {
            // Check current network status
            bool isAvailable = PlatformQuirks::hasNetworkConnectivity();
            fprintf(stderr, "Network status changed: available=%d\n", isAvailable);
            g_networkCallback(isAvailable);
        }
        LeaveCriticalSection(&g_callbackLock);
    }
}

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
    // Suppress Windows "Insert a disk" / "not accessible" system error dialogs
    // for the main thread. This prevents Windows from showing modal dialogs
    // when accessing removable drives that may not have media inserted.
    // Worker threads set their own error mode in their run() methods.
    // Use SetThreadErrorMode (modern API) with fallback to SetErrorMode.
    DWORD oldMode;
    if (!SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode)) {
        // Fallback for older Windows versions
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    }

    // Check for NVIDIA graphics cards and apply software renderer workaround
    if (hasNvidiaGraphicsCard()) {
        SetEnvironmentVariableA("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
    }

    // make imager single instance because of rpi-connect callback server
    // will be automatically released once the process exits cleanly or crashes
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\RaspberryPiImagerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance running
        MessageBoxW(nullptr, L"Raspberry Pi Imager is already running.", L"Raspberry Pi Imager", MB_OK | MB_ICONINFORMATION);
        exit(0);
    }
}

void beep() {
    // Use Windows MessageBeep for system beep sound
    MessageBeep(MB_OK);
}

bool isBeepAvailable() {
    // Windows MessageBeep is always available via kernel32.dll
    return true;
}

bool hasNetworkConnectivity() {
    // Use Windows API to check network connectivity
    // Check if any network adapter has an IP address
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;
    
    // First call to get size
    if (GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &dwSize) != ERROR_BUFFER_OVERFLOW) {
        return false;
    }
    
    PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*) malloc(dwSize);
    if (pAddresses == NULL) {
        return false;
    }
    
    // Get adapter addresses
    dwRetVal = GetAdaptersAddresses(AF_UNSPEC, 
                                   GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                   NULL, pAddresses, &dwSize);
    
    if (dwRetVal != NO_ERROR) {
        free(pAddresses);
        return false;
    }
    
    // Check if any adapter is connected and has an IP address
    bool hasConnectivity = false;
    PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
    
    while (pCurrAddresses) {
        // Skip loopback and tunnel adapters
        if (pCurrAddresses->IfType != IF_TYPE_SOFTWARE_LOOPBACK &&
            pCurrAddresses->OperStatus == IfOperStatusUp) {
            
            // Check if adapter has at least one unicast IP address
            PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
            if (pUnicast != NULL) {
                hasConnectivity = true;
                break;
            }
        }
        pCurrAddresses = pCurrAddresses->Next;
    }
    
    free(pAddresses);
    return hasConnectivity;
}

bool isNetworkReady() {
    // On Windows, no special time sync check needed - system time is reliable
    return hasNetworkConnectivity();
}

void startNetworkMonitoring(NetworkStatusCallback callback) {
    // Stop any existing monitoring
    stopNetworkMonitoring();
    
    // Initialize lock if needed and set callback
    initCallbackLock();
    EnterCriticalSection(&g_callbackLock);
    g_networkCallback = callback;
    LeaveCriticalSection(&g_callbackLock);
    
    // Register for IP interface change notifications
    DWORD result = NotifyIpInterfaceChange(
        AF_UNSPEC,              // Monitor both IPv4 and IPv6
        networkChangeCallback,  // Callback function
        nullptr,                // Context (not used)
        FALSE,                  // Don't trigger initial notification
        &g_notificationHandle
    );
    
    if (result != NO_ERROR) {
        fprintf(stderr, "Failed to register network change notification: %lu\n", result);
        g_notificationHandle = NULL;
        return;
    }
    
    fprintf(stderr, "Network monitoring started\n");
}

void stopNetworkMonitoring() {
    if (g_notificationHandle) {
        CancelMibChangeNotify2(g_notificationHandle);
        g_notificationHandle = NULL;
        fprintf(stderr, "Network monitoring stopped\n");
    }
    
    // Clear callback under lock to prevent race with callback thread
    // initCallbackLock is safe to call even if never initialized
    initCallbackLock();
    EnterCriticalSection(&g_callbackLock);
    g_networkCallback = nullptr;
    LeaveCriticalSection(&g_callbackLock);
}

void bringWindowToForeground(void* windowHandle) {
    if (!windowHandle) {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(windowHandle);

    // Restore the window if it's minimized
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    // Bring the window to the foreground
    // This combination of calls is necessary to reliably bring the window to the front
    // even when called from a different process or thread context
    SetForegroundWindow(hwnd);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    
    // Flash the window if SetForegroundWindow didn't succeed
    // (Windows may prevent stealing focus in some cases)
    FLASHWINFO fwi;
    fwi.cbSize = sizeof(FLASHWINFO);
    fwi.hwnd = hwnd;
    fwi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
    fwi.uCount = 3;
    fwi.dwTimeout = 0;
    FlashWindowEx(&fwi);
}

bool hasElevatedPrivileges() {
    // Check if running as Administrator
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;

    // Allocate and initialize a SID of the administrators group
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&NtAuthority, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0,
                                  &pAdministratorsGroup)) {
        return false;
    }

    // Determine whether the SID of administrators group is enabled in
    // the primary access token of the process
    if (!CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin)) {
        fIsRunAsAdmin = FALSE;
    }

    // Free the SID
    FreeSid(pAdministratorsGroup);

    return fIsRunAsAdmin == TRUE;
}

void attachConsole() {
    // Allocate console on Windows (only needed if compiled as GUI program)
    // Try to attach to parent process console first, or allocate a new one
    if (::AttachConsole(ATTACH_PARENT_PROCESS) || ::AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        // Sync C++ iostreams with C stdio for consistency
        std::ios::sync_with_stdio();
    }
}

bool isElevatableBundle() {
    // Windows uses UAC manifests for elevation, not this mechanism
    return false;
}

const char* getBundlePath() {
    // Not applicable on Windows
    return nullptr;
}

bool hasElevationPolicyInstalled() {
    // Not applicable on Windows
    return false;
}

bool installElevationPolicy() {
    // Not applicable on Windows
    return false;
}

bool tryElevate(int argc, char** argv) {
    // Windows uses UAC and ShellExecute with "runas" verb for elevation
    (void)argc;
    (void)argv;
    return false;
}

bool launchDetached(const QString& program, const QStringList& arguments) {
    // On Windows, QProcess::startDetached works correctly for launching
    // detached processes that outlive the parent
    return QProcess::startDetached(program, arguments);
}

bool runElevatedPolicyInstaller() {
    return false;
}

void execElevated(const QStringList& extraArgs) {
    Q_UNUSED(extraArgs);
}

bool isScrollInverted(bool qtInvertedFlag) {
    // On Windows, Qt doesn't correctly report the scroll direction setting.
    // We read directly from the registry instead of trusting qtInvertedFlag.
    Q_UNUSED(qtInvertedFlag);
    
    // Check Windows registry for scroll direction setting
    // This setting is stored in PrecisionTouchPad for touchpads
    // Value: 0 = natural (down scrolls up), 1 = traditional (down scrolls down)
    
    HKEY hKey;
    DWORD scrollDirection = 1;  // Default to traditional
    DWORD dataSize = sizeof(scrollDirection);
    
    // Check precision touchpad setting
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"ScrollDirection", NULL, NULL,
                        (LPBYTE)&scrollDirection, &dataSize);
        RegCloseKey(hKey);
    }
    
    // 0 = natural scrolling (invert), 1 = traditional (don't invert)
    return scrollDirection == 0;
}

QString getWriteDevicePath(const QString& devicePath) {
    // Windows uses PhysicalDrive paths which don't have a raw device equivalent.
    // Direct I/O is controlled via FILE_FLAG_NO_BUFFERING, not device path.
    return devicePath;
}

QString getEjectDevicePath(const QString& devicePath) {
    // No path transformation needed on Windows.
    return devicePath;
}

// Helper to extract device number from PhysicalDrive path
// Defined outside anonymous namespace for test API access
static int parseDeviceNumberImpl(const QString& device) {
    // Expected format: \\.\PhysicalDriveN
    QByteArray deviceBytes = device.toLower().toUtf8();
    const char* deviceStr = deviceBytes.constData();
    
    int deviceId = -1;
    
    // Try to parse as \\.\PhysicalDriveN
    if (sscanf(deviceStr, "\\\\.\\physicaldrive%d", &deviceId) == 1) {
        return deviceId;
    }
    // Also accept //./PhysicalDriveN (forward slashes)
    if (sscanf(deviceStr, "//./physicaldrive%d", &deviceId) == 1) {
        return deviceId;
    }
    
    return -1;
}

namespace {
    // Get device number from volume handle
    ULONG getDeviceNumberFromHandle(HANDLE volume) {
        STORAGE_DEVICE_NUMBER storageDeviceNumber;
        DWORD bytesReturned;
        
        if (!DeviceIoControl(volume, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                            NULL, 0,
                            &storageDeviceNumber, sizeof(storageDeviceNumber),
                            &bytesReturned, NULL)) {
            return ULONG_MAX;
        }
        return storageDeviceNumber.DeviceNumber;
    }
    
    // Lock a volume for exclusive access
    bool lockVolume(HANDLE volume) {
        DWORD bytesReturned;
        for (int tries = 0; tries < 20; tries++) {
            if (DeviceIoControl(volume, FSCTL_LOCK_VOLUME,
                              NULL, 0, NULL, 0,
                              &bytesReturned, NULL)) {
                return true;
            }
            Sleep(500);
        }
        return false;
    }
    
    // Unlock a volume
    bool unlockVolume(HANDLE volume) {
        DWORD bytesReturned;
        return DeviceIoControl(volume, FSCTL_UNLOCK_VOLUME,
                              NULL, 0, NULL, 0,
                              &bytesReturned, NULL) != FALSE;
    }
    
    // Dismount a volume (flush and invalidate)
    bool dismountVolume(HANDLE volume) {
        DWORD bytesReturned;
        return DeviceIoControl(volume, FSCTL_DISMOUNT_VOLUME,
                              NULL, 0, NULL, 0,
                              &bytesReturned, NULL) != FALSE;
    }
    
    // Check if volume is mounted
    bool isVolumeMounted(HANDLE volume) {
        DWORD bytesReturned;
        return DeviceIoControl(volume, FSCTL_IS_VOLUME_MOUNTED,
                              NULL, 0, NULL, 0,
                              &bytesReturned, NULL) != FALSE;
    }
    
    // Eject media from volume (card, not reader!)
    bool ejectMedia(HANDLE volume) {
        DWORD bytesReturned;
        
        // First, allow media removal
        PREVENT_MEDIA_REMOVAL buffer;
        buffer.PreventMediaRemoval = FALSE;
        DeviceIoControl(volume, IOCTL_STORAGE_MEDIA_REMOVAL,
                       &buffer, sizeof(buffer),
                       NULL, 0, &bytesReturned, NULL);
        
        // Then eject the media - this ejects the CARD, not the card reader!
        // This is the key fix: IOCTL_STORAGE_EJECT_MEDIA ejects the removable media
        // (the SD card) while leaving the card reader attached to the system.
        for (int tries = 0; tries < 5; tries++) {
            if (tries > 0) {
                Sleep(500);
            }
            if (DeviceIoControl(volume, IOCTL_STORAGE_EJECT_MEDIA,
                               NULL, 0, NULL, 0,
                               &bytesReturned, NULL)) {
                return true;
            }
        }
        return false;
    }
    
    // Process a single drive letter: unmount/eject if it belongs to our device
    PlatformQuirks::DiskResult processDriveLetter(TCHAR driveLetter, ULONG targetDeviceNumber, bool doEject) {
        // Open volume handle
        TCHAR volumePath[8];
        swprintf_s(volumePath, 8, L"\\\\.\\%c:", driveLetter);
        
        HANDLE volume = CreateFileW(volumePath,
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, 0, NULL);
        
        if (volume == INVALID_HANDLE_VALUE) {
            // Can't open - might not exist or no permissions
            return PlatformQuirks::DiskResult::Success;  // Skip this letter
        }
        
        // Check if this volume belongs to our target device
        ULONG volumeDeviceNumber = getDeviceNumberFromHandle(volume);
        if (volumeDeviceNumber != targetDeviceNumber) {
            CloseHandle(volume);
            return PlatformQuirks::DiskResult::Success;  // Not our device
        }
        
        qDebug() << "processDriveLetter:" << QString(QChar(driveLetter)) << "belongs to device" << targetDeviceNumber;
        
        // Check if mounted
        if (!isVolumeMounted(volume)) {
            qDebug() << "processDriveLetter:" << QString(QChar(driveLetter)) << "not mounted";
            CloseHandle(volume);
            return PlatformQuirks::DiskResult::Success;
        }
        
        // Lock the volume
        if (!lockVolume(volume)) {
            qDebug() << "processDriveLetter: couldn't lock" << QString(QChar(driveLetter));
            CloseHandle(volume);
            return PlatformQuirks::DiskResult::Busy;
        }
        
        // Dismount the volume
        if (!dismountVolume(volume)) {
            qDebug() << "processDriveLetter: couldn't dismount" << QString(QChar(driveLetter));
            unlockVolume(volume);
            CloseHandle(volume);
            return PlatformQuirks::DiskResult::Error;
        }
        
        // If ejecting, eject the media
        if (doEject) {
            if (!ejectMedia(volume)) {
                qDebug() << "processDriveLetter: couldn't eject media from" << QString(QChar(driveLetter));
                // Not fatal - volume is still dismounted
            }
        }
        
        unlockVolume(volume);
        CloseHandle(volume);
        
        qDebug() << "processDriveLetter: successfully processed" << QString(QChar(driveLetter));
        return PlatformQuirks::DiskResult::Success;
    }
}

DiskResult unmountDisk(const QString& device) {
    int deviceNumber = parseDeviceNumberImpl(device);
    if (deviceNumber < 0) {
        qDebug() << "unmountDisk: invalid device path" << device;
        return DiskResult::InvalidDrive;
    }
    
    qDebug() << "unmountDisk: processing device" << deviceNumber;
    
    // Get all logical drives
    DWORD drivesMask = GetLogicalDrives();
    if (drivesMask == 0) {
        qDebug() << "unmountDisk: couldn't get logical drives";
        return DiskResult::Error;
    }
    
    // Process each drive letter
    TCHAR driveLetter = L'A';
    DiskResult result = DiskResult::Success;
    
    while (drivesMask) {
        if (drivesMask & 1) {
            DiskResult letterResult = processDriveLetter(driveLetter, deviceNumber, false);
            if (letterResult != DiskResult::Success && result == DiskResult::Success) {
                result = letterResult;  // Remember first error
            }
        }
        driveLetter++;
        drivesMask >>= 1;
    }
    
    return result;
}

DiskResult ejectDisk(const QString& device) {
    int deviceNumber = parseDeviceNumberImpl(device);
    if (deviceNumber < 0) {
        qDebug() << "ejectDisk: invalid device path" << device;
        return DiskResult::InvalidDrive;
    }
    
    qDebug() << "ejectDisk: ejecting device" << deviceNumber;
    
    // Get all logical drives
    DWORD drivesMask = GetLogicalDrives();
    if (drivesMask == 0) {
        qDebug() << "ejectDisk: couldn't get logical drives";
        return DiskResult::Error;
    }
    
    // Process each drive letter with eject
    TCHAR driveLetter = L'A';
    DiskResult result = DiskResult::Success;
    bool ejectedAny = false;
    
    while (drivesMask) {
        if (drivesMask & 1) {
            DiskResult letterResult = processDriveLetter(driveLetter, deviceNumber, true);
            if (letterResult == DiskResult::Success) {
                ejectedAny = true;
            } else if (result == DiskResult::Success) {
                result = letterResult;  // Remember first error
            }
        }
        driveLetter++;
        drivesMask >>= 1;
    }
    
    // If we didn't find any volumes but the device exists, try direct eject on the physical drive
    if (!ejectedAny) {
        QString physicalPath = QString("\\\\.\\PhysicalDrive%1").arg(deviceNumber);
        HANDLE physicalDrive = CreateFileW(
            reinterpret_cast<LPCWSTR>(physicalPath.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);
        
        if (physicalDrive != INVALID_HANDLE_VALUE) {
            qDebug() << "ejectDisk: attempting direct eject on physical drive";
            if (ejectMedia(physicalDrive)) {
                qDebug() << "ejectDisk: direct eject succeeded";
                result = DiskResult::Success;
            }
            CloseHandle(physicalDrive);
        }
    }
    
    return result;
}

// Test API for unit testing internal functions
#ifdef PLATFORMQUIRKS_ENABLE_TEST_API
namespace TestAPI {
    int parseDeviceNumber(const QString& device) {
        return parseDeviceNumberImpl(device);
    }
}
#endif

} // namespace PlatformQuirks
