// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "helper_log.h"

#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>

namespace rpi_imager::writer {

namespace {

std::mutex g_log_mutex;

const char* logPath() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    static std::string path;
    if (runtime && runtime[0]) {
        path = std::string(runtime) + "/rpi-imager/helper.log";
        return path.c_str();
    }
    return "/tmp/rpi-imager-helper.log";
}

} // namespace

void helperLog(const char* msg) {
    if (!msg) return;
    std::lock_guard<std::mutex> lk(g_log_mutex);
    const char* path = logPath();
    if (const char* slash = std::strrchr(path, '/')) {
        std::string dir(path, slash - path);
        ::mkdir(dir.c_str(), 0700);
    }
    std::FILE* f = std::fopen(path, "a");
    if (!f) return;
    std::time_t now = std::time(nullptr);
    char tbuf[32] = {0};
    std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(f, "[%s] %s\n", tbuf, msg);
    std::fclose(f);
}

} // namespace rpi_imager::writer
