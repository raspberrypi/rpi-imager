// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "proc_environ.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace rpi_imager::linux_proc {

std::string environValue(pid_t pid, const char* key) {
    if (pid <= 0 || !key || !key[0]) {
        return {};
    }

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/environ", static_cast<int>(pid));

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }

    std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string prefix = std::string(key) + '=';

    for (std::size_t i = 0; i < blob.size();) {
        const std::size_t end = blob.find('\0', i);
        const std::size_t len = (end == std::string::npos) ? blob.size() - i : end - i;
        const std::string entry(blob.data() + i, len);
        if (entry.rfind(prefix, 0) == 0) {
            return entry.substr(prefix.size());
        }
        if (end == std::string::npos) {
            break;
        }
        i = end + 1;
    }
    return {};
}

} // namespace rpi_imager::linux_proc
