// SPDX-License-Identifier: Apache-2.0
// Tiny Windows callback URL relay for rpi-imager

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <strsafe.h>
#pragma comment(lib, "ws2_32.lib")

#ifndef RPI_IMAGER_PORT
#define RPI_IMAGER_PORT 49629
#endif
#ifndef RPI_IMAGER_EXE_NAME
#define RPI_IMAGER_EXE_NAME L"rpi-imager.exe"
#endif
// If nonzero: on IPC failure, start Imager with the URL as positional arg.
#ifndef RPI_IMAGER_START_ON_FAIL
#define RPI_IMAGER_START_ON_FAIL 1
#endif

static int send_url_over_tcp_utf8(const wchar_t* urlW)
{
    int rc = 1;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 1;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { WSACleanup(); return 1; }

    // short connect timeout using non-blocking + select
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(RPI_IMAGER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int c = connect(s, (sockaddr*)&addr, sizeof(addr));
    if (c == SOCKET_ERROR) {
        fd_set wfds; FD_ZERO(&wfds); FD_SET(s, &wfds);
        timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 300 * 1000; // 300ms
        if (select(0, nullptr, &wfds, nullptr, &tv) <= 0) { closesocket(s); WSACleanup(); return 2; }
        int err = 0; socklen_t len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        if (err != 0) { closesocket(s); WSACleanup(); return 2; }
    }

    // back to blocking for send
    nb = 0; ioctlsocket(s, FIONBIO, &nb);

    // Convert URL (UTF-16) -> UTF-8
    int need = WideCharToMultiByte(CP_UTF8, 0, urlW, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1 || need > 8192) { // Sanity check: reasonable max size
        closesocket(s); WSACleanup(); return 1;
    }
    
    // Send without trailing null
    int bytes = need - 1;
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!buf) { closesocket(s); WSACleanup(); return 1; }
    
    int converted = WideCharToMultiByte(CP_UTF8, 0, urlW, -1, buf, need, nullptr, nullptr);
    if (converted == 0) {
        // Conversion failed
        HeapFree(GetProcessHeap(), 0, buf);
        closesocket(s);
        WSACleanup();
        return 1;
    }

    // Write all
    int sentTot = 0;
    while (sentTot < bytes) {
        int n = send(s, buf + sentTot, bytes - sentTot, 0);
        if (n <= 0) { rc = 2; break; }
        sentTot += n;
    }

    HeapFree(GetProcessHeap(), 0, buf);
    closesocket(s);
    WSACleanup();
    return rc == 2 ? 2 : 0;
}

static void start_imager_with_url(const wchar_t* urlW)
{
    // Launch {app}\rpi-imager.exe "<url>"
    // Working dir = directory containing this relay
    wchar_t exePath[MAX_PATH];
    DWORD pathLen = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    // Check for errors and truncation
    if (pathLen == 0 || pathLen >= MAX_PATH) {
        return; // Failed or path too long
    }
    
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (!lastSlash) return;
    *lastSlash = L'\0'; // strip exe name, leave folder
    
    wchar_t imagerExe[MAX_PATH];
    HRESULT hr = StringCchPrintfW(imagerExe, _countof(imagerExe), L"%s\\%s", exePath, RPI_IMAGER_EXE_NAME);
    if (FAILED(hr)) {
        return; // Path construction failed (too long)
    }

    // Verify the target executable exists before launching
    DWORD attrs = GetFileAttributesW(imagerExe);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return; // File doesn't exist or is a directory
    }

    // Build command line: "rpi-imager.exe" "<url>"
    // Use ShellExecuteEx to respect UAC manifest of rpi-imager.exe
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI; // No error UI on failure
    sei.hwnd = nullptr;
    sei.lpVerb = L"open";
    sei.lpFile = imagerExe;
    sei.lpParameters = urlW;     // single positional argument
    sei.lpDirectory = exePath;   // working dir = app folder
    sei.nShow = SW_SHOWNORMAL;

    ShellExecuteExW(&sei);
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR cmdLine, int)
{
    // Windows passes the URL as %1, which appears in cmdLine for ShellExecute-based calls.
    const wchar_t* urlW = cmdLine;
    
    // If cmdLine is empty, parse GetCommandLineW() to extract first argument
    if (!urlW || !*urlW) {
        urlW = GetCommandLineW();
        // Skip executable path (handles quoted and unquoted paths)
        if (urlW && *urlW == L'"') {
            urlW = wcschr(urlW + 1, L'"');
            if (urlW) urlW++;
        } else if (urlW) {
            while (*urlW && *urlW != L' ') urlW++;
        }
        // Skip spaces to get to first argument
        while (urlW && *urlW == L' ') urlW++;
    }

    if (!urlW || !*urlW) return 0;

    // Trim surrounding quotes if present and copy to buffer for safety
    wchar_t urlBuf[2048];
    if (*urlW == L'"') {
        // Copy and strip quotes
        const wchar_t* start = urlW + 1;
        const wchar_t* end = wcsrchr(start, L'"');
        if (end) {
            size_t len = end - start;
            if (len > 0 && len < _countof(urlBuf)) {
                wcsncpy_s(urlBuf, _countof(urlBuf), start, len);
                urlBuf[len] = L'\0';
                urlW = urlBuf;
            } else {
                return 0; // URL too long or empty after quote stripping
            }
        } else {
            return 0; // Malformed: opening quote but no closing quote
        }
    } else {
        // Not quoted - copy to buffer for consistent handling and length validation
        size_t len = wcsnlen_s(urlW, _countof(urlBuf));
        if (len == 0 || len >= _countof(urlBuf)) {
            return 0; // Empty or too long
        }
        wcsncpy_s(urlBuf, _countof(urlBuf), urlW, len);
        urlBuf[len] = L'\0';
        urlW = urlBuf;
    }

    // Validate: must start with rpi-imager://
    if (wcsncmp(urlW, L"rpi-imager://", 13) != 0) {
        return 0;
    }

    // Additional validation: check for reasonable URL length (after protocol)
    size_t totalLen = wcslen(urlW);
    if (totalLen < 14 || totalLen > 2000) { // min: rpi-imager://x, max: reasonable limit
        return 0;
    }

    // Validate: no control characters or unexpected chars in URL
    for (size_t i = 0; i < totalLen; i++) {
        wchar_t c = urlW[i];
        // Allow only printable ASCII, common URL chars, and some UTF-8
        // Block control chars (0x00-0x1F, 0x7F), and dangerous shell chars if any slip through
        if (c < 0x20 || c == 0x7F) {
            return 0; // Control character detected
        }
    }

    int r = send_url_over_tcp_utf8(urlW);
#if RPI_IMAGER_START_ON_FAIL
    if (r != 0) {
        start_imager_with_url(urlW);
        return 0;
    }
#endif
    return 0;
}
