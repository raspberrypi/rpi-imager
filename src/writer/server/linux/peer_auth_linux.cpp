// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "peer_auth_linux.h"

#include "rpi_imager_identity.h"

#include "../../../linux/appimage_signature.h"
#include "../../../linux/proc_environ.h"

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

namespace rpi_imager::writer {

namespace {

std::string exePathForPid(pid_t pid) {
    char link[64];
    std::snprintf(link, sizeof(link), "/proc/%d/exe", static_cast<int>(pid));
    char buf[4096] = {0};
    const ssize_t n = ::readlink(link, buf, sizeof(buf) - 1);
    if (n <= 0) {
        return {};
    }
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string dirnameOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string basenameOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::vector<std::string> trustedAppImageKeyFingerprints() {
    std::vector<std::string> out;
    out.reserve(identity::kTrustedAppImageKeyFingerprintCount);
    for (std::size_t i = 0; i < identity::kTrustedAppImageKeyFingerprintCount; ++i) {
        out.emplace_back(identity::kTrustedAppImageKeyFingerprints[i]);
    }
    return out;
}

bool verifyClientAppImage(const std::string& appimage_path) {
    const auto trusted = trustedAppImageKeyFingerprints();
    const auto result = appimage::verifyEmbeddedSignature(appimage_path, trusted);
    if (trusted.empty()) {
        if (result == appimage::VerifyResult::Unsigned) {
            std::fprintf(stderr,
                         "peer auth: unsigned AppImage allowed (no pinned keys): %s\n",
                         appimage_path.c_str());
            return true;
        }
        return result == appimage::VerifyResult::Ok;
    }
    if (result != appimage::VerifyResult::Ok) {
        std::fprintf(stderr,
                     "peer auth: AppImage signature check failed (%s): %s\n",
                     appimage::verifyResultMessage(result),
                     appimage_path.c_str());
        return false;
    }
    return true;
}

bool verifyCoInstalledNativeClient(pid_t client_pid) {
    const std::string client_exe = exePathForPid(client_pid);
    const std::string helper_exe = exePathForPid(::getpid());
    if (client_exe.empty() || helper_exe.empty()) {
        return false;
    }

    const std::string base = basenameOf(client_exe);
    if (base != "rpi-imager" && base != "rpi-imager-cli") {
        std::fprintf(stderr, "peer auth: rejected client exe %s\n", client_exe.c_str());
        return false;
    }

    if (dirnameOf(client_exe) != dirnameOf(helper_exe)) {
        std::fprintf(stderr,
                     "peer auth: client %s not co-located with helper %s\n",
                     client_exe.c_str(), helper_exe.c_str());
        return false;
    }
    return true;
}

} // namespace

bool verifyConnectingClient(pid_t client_pid) {
    if (client_pid <= 0) {
        return false;
    }

    const std::string client_appimage =
        linux_proc::environValue(client_pid, "APPIMAGE");
    if (!client_appimage.empty()) {
        if (!verifyClientAppImage(client_appimage)) {
            return false;
        }
        const std::string helper_appimage = linux_proc::environValue(::getpid(), "APPIMAGE");
        if (!helper_appimage.empty() && helper_appimage != client_appimage) {
            std::fprintf(stderr,
                         "peer auth: helper AppImage %s != client %s\n",
                         helper_appimage.c_str(), client_appimage.c_str());
            return false;
        }
        return true;
    }

    if (identity::kTrustedAppImageKeyFingerprintCount > 0) {
        std::fprintf(stderr,
                     "peer auth: client pid %d has no APPIMAGE but release keys are pinned\n",
                     static_cast<int>(client_pid));
        return false;
    }

    return verifyCoInstalledNativeClient(client_pid);
}

} // namespace rpi_imager::writer
