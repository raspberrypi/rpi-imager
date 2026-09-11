/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for ProcessScopedSuspendInhibitor, which is what stops the machine
 * going to sleep while a card is being written.
 *
 * It holds the inhibit by running the platform's inhibitor tool wrapped
 * around `cat` on a FIFO, and releases it by closing the write end. The FIFO
 * lives in /run, which an ordinary user cannot write to, so none of it can be
 * reached from the test binary: mkfifo fails and the constructor gives up
 * before it has done anything. The driver runs this under `unshare -rm` with
 * a tmpfs over /run, where it can.
 */

#include "linux/linux_suspend_inhibitor.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char *kFifoDir    = "/run";
constexpr const char *kFifoPrefix = "rpi-imager-suspend_";

// The inhibitor's FIFOs, by name. It picks a random one each time, so the
// only way to know which is ours is to look before and after.
std::vector<std::string> fifos()
{
    std::vector<std::string> out;
    DIR *d = ::opendir(kFifoDir);
    if (!d)
        return out;
    while (const dirent *e = ::readdir(d)) {
        if (std::strncmp(e->d_name, kFifoPrefix, std::strlen(kFifoPrefix)) != 0)
            continue;
        std::string path = std::string(kFifoDir) + "/" + e->d_name;
        struct stat st {};
        if (::lstat(path.c_str(), &st) == 0 && S_ISFIFO(st.st_mode))
            out.push_back(std::move(path));
    }
    ::closedir(d);
    return out;
}

// How many live children this process has. A child that has exited but not
// been reaped is a zombie and still counted, which is the point: leaving one
// behind is as much a leak as leaving the process running.
int children()
{
    const pid_t me = ::getpid();
    int count = 0;
    DIR *d = ::opendir("/proc");
    if (!d)
        return -1;
    while (const dirent *e = ::readdir(d)) {
        const char *p = e->d_name;
        if (*p < '0' || *p > '9')
            continue;
        char statPath[64];
        std::snprintf(statPath, sizeof(statPath), "/proc/%s/stat", p);
        FILE *f = std::fopen(statPath, "r");
        if (!f)
            continue;
        char line[512] = {0};
        const size_t n = std::fread(line, 1, sizeof(line) - 1, f);
        std::fclose(f);
        if (n == 0)
            continue;
        // The command name is in parentheses and may contain anything,
        // spaces included; the fields after it start at the last ')'.
        const char *after = std::strrchr(line, ')');
        if (!after)
            continue;
        int ppid = 0;
        if (std::sscanf(after + 1, " %*c %d", &ppid) == 1 && ppid == me)
            ++count;
    }
    ::closedir(d);
    return count;
}

long millisSince(const std::chrono::steady_clock::time_point &from)
{
    using namespace std::chrono;
    return long(duration_cast<milliseconds>(steady_clock::now() - from).count());
}

} // namespace

int main(int argc, char *argv[])
{
    int arg = 1;

    // --squeeze: the two ways creating the control FIFO goes wrong, which
    // need the same one trick to arrange.
    bool squeeze = false;
    if (arg < argc && std::strcmp(argv[arg], "--squeeze") == 0) {
        squeeze = true;
        ++arg;
    }

    if (argc - arg < 1) {
        std::fprintf(stderr, "usage: %s [--squeeze] <tool> [args...]\n", argv[0]);
        return 2;
    }

    const char *tool = argv[arg++];
    std::vector<std::string> args;
    for (int i = arg; i < argc; ++i)
        args.emplace_back(argv[i]);

    const size_t before = fifos().size();
    std::printf("FIFOS_BEFORE=%zu\n", before);

    struct rlimit savedFiles {};
    if (squeeze) {
        // Attempt zero's name, given the fallback: pid ^ (0 * 65537).
        char planted[256];
        std::snprintf(planted, sizeof(planted), "%s/%s%lx",
                      kFifoDir, kFifoPrefix, static_cast<unsigned long>(::getpid()));
        const int planted_rc = ::mkfifo(planted, 0600);
        std::printf("PLANTED=%d\n", planted_rc == 0 ? 1 : 0);

        if (::getrlimit(RLIMIT_NOFILE, &savedFiles) != 0)
            return 2;
        struct rlimit tight = savedFiles;
        tight.rlim_cur = 3;   // stdin, stdout, stderr, and nothing spare
        if (::setrlimit(RLIMIT_NOFILE, &tight) != 0)
            return 2;
    }

    std::optional<ProcessScopedSuspendInhibitor> inhibitor;
    inhibitor.emplace(tool, args);

    if (squeeze) {
        // Put the limit back before anything wants a descriptor to report
        // with. What was measured is already done.
        ::setrlimit(RLIMIT_NOFILE, &savedFiles);
    }

    std::printf("FIFOS_DURING=%zu\n", fifos().size());
    std::printf("CHILDREN_DURING=%d\n", children());
    std::fflush(stdout);

    // Releasing the inhibit is the measurement: it is what runs when the
    // write has finished and the application is on its way out.
    const auto started = std::chrono::steady_clock::now();
    inhibitor.reset();
    const long cleanupMs = millisSince(started);

    std::printf("CLEANUP_MS=%ld\n", cleanupMs);
    std::printf("FIFOS_AFTER=%zu\n", fifos().size());
    std::printf("CHILDREN_AFTER=%d\n", children());
    std::fflush(stdout);
    return 0;
}
