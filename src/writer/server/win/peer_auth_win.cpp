// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "peer_auth_win.h"
#include "rpi_imager_identity.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>

#include <string>

namespace rpi_imager::writer {

namespace {

using rpi_imager::identity::kPublisherOrganizationWide;
using rpi_imager::identity::kTrustedSignerThumbprintCount;
using rpi_imager::identity::kTrustedSignerThumbprints;
using rpi_imager::identity::kWindowsClientExeName;

// Case-insensitive wide-string equality.
bool iequals(const wchar_t* a, const wchar_t* b) {
    if (!a || !b) return false;
    return CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, a, -1, b, -1) == CSTR_EQUAL;
}

// Returns the final path component of `path` (no trailing slash required).
std::wstring basename(const wchar_t* path) {
    if (!path || !path[0]) return std::wstring();
    const wchar_t* slash = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') slash = p + 1;
    }
    return std::wstring(slash);
}

// Parent directory of `path`, including trailing separator, or empty on failure.
std::wstring parentDirectory(const wchar_t* path) {
    if (!path || !path[0]) return std::wstring();
    std::wstring dir(path);
    const auto pos = dir.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    dir.resize(pos + 1);
    return dir;
}

bool sameInstallDirectory(const wchar_t* client_path) {
    wchar_t helper_path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, helper_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    const std::wstring helper_dir = parentDirectory(helper_path);
    const std::wstring client_dir = parentDirectory(client_path);
    if (helper_dir.empty() || client_dir.empty()) return false;
    return iequals(helper_dir.c_str(), client_dir.c_str());
}

// Reads the Organization (O=) attribute from the leaf Authenticode signer cert.
bool readCertOrganization(PCCERT_CONTEXT cert, std::wstring& out_org) {
    if (!cert) return false;
    const DWORD need = CertGetNameStringW(
        cert, CERT_NAME_ATTR_TYPE, 0, const_cast<void*>(static_cast<const void*>(szOID_ORGANIZATION_NAME)),
        nullptr, 0);
    if (need <= 1) return false;
    out_org.assign(need, L'\0');
    const DWORD wrote = CertGetNameStringW(
        cert, CERT_NAME_ATTR_TYPE, 0, const_cast<void*>(static_cast<const void*>(szOID_ORGANIZATION_NAME)),
        out_org.data(), need);
    if (wrote <= 1) {
        out_org.clear();
        return false;
    }
    if (!out_org.empty() && out_org.back() == L'\0') {
        out_org.pop_back();
    }
    return !out_org.empty();
}

// Formats CERT_SHA1_HASH_PROP_ID as uppercase hex (matches PowerShell Thumbprint).
bool readCertSha1ThumbprintHex(PCCERT_CONTEXT cert, std::wstring& out_hex) {
    if (!cert) return false;
    DWORD hash_len = 0;
    if (!CertGetCertificateContextProperty(
            cert, CERT_SHA1_HASH_PROP_ID, nullptr, &hash_len) ||
        hash_len == 0) {
        return false;
    }
    BYTE hash[32] = {};
    if (hash_len > sizeof(hash)) return false;
    if (!CertGetCertificateContextProperty(
            cert, CERT_SHA1_HASH_PROP_ID, hash, &hash_len)) {
        return false;
    }
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    out_hex.resize(hash_len * 2);
    for (DWORD i = 0; i < hash_len; ++i) {
        out_hex[i * 2] = kHex[(hash[i] >> 4) & 0xF];
        out_hex[i * 2 + 1] = kHex[hash[i] & 0xF];
    }
    return true;
}

bool thumbprintMatchesAllowlist(const std::wstring& thumbprint) {
    if (kTrustedSignerThumbprintCount == 0) {
        return true;
    }
    for (size_t i = 0; i < kTrustedSignerThumbprintCount; ++i) {
        if (iequals(thumbprint.c_str(), kTrustedSignerThumbprints[i])) {
            return true;
        }
    }
    return false;
}

// After a successful WinVerifyTrust, inspect the provider signer chain.
bool publisherMatchesPinnedIdentity(HANDLE wvt_state) {
    CRYPT_PROVIDER_DATA* prov = WTHelperProvDataFromStateData(wvt_state);
    if (!prov || prov->csSigners == 0) return false;

    CRYPT_PROVIDER_SGNR* signer =
        WTHelperGetProvSignerFromChain(prov, 0, FALSE, 0);
    if (!signer || signer->csCertChain == 0) return false;

    CRYPT_PROVIDER_CERT* prov_cert = WTHelperGetProvCertFromChain(signer, 0);
    if (!prov_cert || !prov_cert->pCert) return false;

    std::wstring org;
    if (!readCertOrganization(prov_cert->pCert, org)) return false;
    if (!iequals(org.c_str(), kPublisherOrganizationWide)) return false;

    std::wstring thumbprint;
    if (!readCertSha1ThumbprintHex(prov_cert->pCert, thumbprint)) return false;
    return thumbprintMatchesAllowlist(thumbprint);
}

bool authenticodePublisherPinned(const wchar_t* image_path) {
    WINTRUST_FILE_INFO file_info{};
    file_info.cbStruct = sizeof(file_info);
    file_info.pcwszFilePath = image_path;

    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file_info;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;

    static const GUID kVerifyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(nullptr, &kVerifyGuid, &trust);

    bool publisher_ok = false;
    if (status == ERROR_SUCCESS) {
        publisher_ok = publisherMatchesPinnedIdentity(trust.hWVTStateData);
    }

    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &kVerifyGuid, &trust);

    return status == ERROR_SUCCESS && publisher_ok;
}

} // namespace

bool verifyConnectingClient(DWORD client_pid) {
    if (client_pid == 0) {
        return false;
    }

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, client_pid);
    if (proc == nullptr) {
        return false;
    }

    wchar_t client_path[MAX_PATH] = {};
    DWORD path_len = MAX_PATH;
    const BOOL got_name = QueryFullProcessImageNameW(proc, 0, client_path, &path_len);
    CloseHandle(proc);
    if (!got_name) {
        return false;
    }

    if (!iequals(basename(client_path.c_str()).c_str(), kWindowsClientExeName)) {
        return false;
    }
    if (!sameInstallDirectory(client_path)) {
        return false;
    }
    return authenticodePublisherPinned(client_path);
}

} // namespace rpi_imager::writer
