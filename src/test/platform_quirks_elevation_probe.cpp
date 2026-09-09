/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for two things in PlatformQuirks that cannot be reached in-process:
 * the sudo/pkexec identity handover in applyQuirks(), and -- with "policy" as
 * its argument -- whether a polkit policy authorising this binary is
 * installed.
 *
 * Run as root, applyQuirks() reads SUDO_UID or PKEXEC_UID and repoints HOME
 * and the XDG directories at the user who invoked it. Without that, every
 * setting, the OS list cache and the downloaded image all land under /root:
 * the user's preferences vanish on the next launch and the cache is rewritten
 * as files they cannot read.
 *
 * The function only does any of this when euid is 0, so it cannot be reached
 * from the test binary. This prints the environment before and after the call
 * and the driver runs it under sudo. Its own diagnostics go to stderr, where
 * applyQuirks() also writes; only these KEY=value lines go to stdout.
 *
 * Deliberately constructs no QCoreApplication, matching the production call
 * site in main.cpp, which runs before any application object exists.
 */

#include "platformquirks.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <unistd.h>

namespace {

void report(const char *label, const char *name)
{
    const char *value = ::getenv(name);
    std::printf("%s%s=%s\n", label, name, value ? value : "");
}

} // namespace

int main(int argc, char *argv[])
{
    // Second mode: whether a polkit policy authorising this binary is
    // installed. The directories it scans are absolute, so the caller
    // bind-mounts synthetic ones over them and runs this inside.
    // Third mode: install a policy for this binary. Only does anything when
    // euid is 0, which inside unshare -r it is.
    if (argc > 1 && std::string_view(argv[1]) == "install") {
        std::printf("BUNDLE=%s\n", PlatformQuirks::getBundlePath());
        std::printf("INSTALLED=%d\n",
                    PlatformQuirks::installElevationPolicy() ? 1 : 0);
        std::fflush(stdout);
        return 0;
    }

    // Fourth mode: self-elevation. tryElevate() forks /usr/bin/pkexec and
    // then decides what to do with the child's exit status -- which is where
    // the user pressing Cancel on the password prompt is told apart from an
    // elevated run that finished, and from one that died. Getting that wrong
    // either kills Imager when somebody declines the prompt or leaves two
    // copies of it on screen when they accept.
    //
    // Nothing here can be reached from the test binary: the function refuses
    // to act as root, and every branch that hands the child's status on calls
    // _exit(). So the caller drops to an ordinary uid inside a namespace,
    // puts a script of its own choosing at /usr/bin/pkexec, and reads this
    // process's exit code -- 40 only when tryElevate() returned rather than
    // exiting, with RET saying which way.
    if (argc > 1 && std::string_view(argv[1]) == "elevate") {
        const bool elevated = PlatformQuirks::tryElevate(argc, argv);
        std::printf("EUID=%lu\n", static_cast<unsigned long>(::geteuid()));
        std::printf("RET=%d\n", elevated ? 1 : 0);
        std::fflush(stdout);
        return 40;
    }

    if (argc > 1 && std::string_view(argv[1]) == "policy") {
        std::printf("BUNDLE=%s\n", PlatformQuirks::getBundlePath());
        std::printf("POLICY=%d\n",
                    PlatformQuirks::hasElevationPolicyInstalled() ? 1 : 0);
        std::fflush(stdout);
        return 0;
    }

    std::printf("EUID=%lu\n", static_cast<unsigned long>(::geteuid()));
    report("BEFORE_", "HOME");

    PlatformQuirks::applyQuirks();

    report("AFTER_", "HOME");
    report("AFTER_", "XDG_CACHE_HOME");
    report("AFTER_", "XDG_CONFIG_HOME");
    report("AFTER_", "XDG_DATA_HOME");
    report("AFTER_", "XDG_RUNTIME_DIR");
    report("AFTER_", "DBUS_SESSION_BUS_ADDRESS");
    report("AFTER_", "DISPLAY");
    report("AFTER_", "WAYLAND_DISPLAY");
    std::fflush(stdout);
    return 0;
}
