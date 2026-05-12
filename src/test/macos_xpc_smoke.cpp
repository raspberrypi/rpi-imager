// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// macos_xpc_smoke - phase 1b proof-of-architecture smoke test.
//
// Drives MacOSXpcBackend directly to validate that the cross-process
// XPC boundary works end-to-end. NOT a unit test - it requires the
// rpi-imager-writer helper to be bootstrapped via launchctl (see
// src/writer/dev/com.raspberrypi.rpi-imager.writer.plist.in).
//
// Skipped from CTest discovery because it depends on out-of-band setup;
// run manually as:
//
//     build/test/macos_xpc_smoke
//
// Asserts:
//   - Helper handshake succeeds (queryHelperStatus returns a version)
//   - unmount() of a non-existent device returns a structured failure
//     (not a crash, not a hang)
//
// Both observations together prove the protocol round-trips correctly.

#include "privileged_io/privileged_writer.h"
#include "privileged_io/backends/macos_xpc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace priv = rpi_imager::privileged;

namespace {

bool hasFlag(int argc, const char* argv[], const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

const char* firstNonFlagArg(int argc, const char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') return argv[i];
    }
    return nullptr;
}

// Write a recognisable pattern at `offset` of length `len`, then read
// back and verify. Returns 0 on success, non-zero on failure.
//
// Pattern: ASCII "RPI-IMAGER-TEST" repeated, with a 16-byte trailer
// containing the offset (so multiple test offsets don't false-match).
int doWriteVerify(priv::IPrivilegedWriter& backend,
                   const priv::proto::SessionId& sid,
                   std::uint64_t offset,
                   std::size_t len) {
    std::vector<std::byte> pattern(len);
    constexpr char kSig[] = "RPI-IMAGER-TEST-";
    constexpr std::size_t kSigLen = sizeof(kSig) - 1;  // exclude NUL
    for (std::size_t i = 0; i < len; ++i) {
        pattern[i] = static_cast<std::byte>(kSig[i % kSigLen]);
    }
    // Stamp the offset into the last 16 bytes so we can tell offsets apart.
    if (len >= 16) {
        char buf[17] = {0};
        std::snprintf(buf, sizeof(buf), "%016llx",
                      (unsigned long long)offset);
        for (std::size_t i = 0; i < 16; ++i) {
            pattern[len - 16 + i] = static_cast<std::byte>(buf[i]);
        }
    }

    std::printf("\n=== writeChunk(offset=%llu, len=%zu) ===\n",
                (unsigned long long)offset, len);
    auto wr = backend.writeChunk(sid, offset, pattern.data(), pattern.size());
    if (!wr.ok) {
        std::printf("FAIL: %s (code %d, kerrno %d)\n",
                    wr.error.detail().c_str(),
                    wr.error.code(),
                    wr.error.kernel_errno());
        return 1;
    }
    std::printf("OK: %zu bytes written\n", wr.value);

    std::printf("\n=== readChunk verify ===\n");
    std::vector<std::byte> readback(len, std::byte{0});
    auto rr = backend.readChunk(sid, offset, readback.data(), readback.size());
    if (!rr.ok) {
        std::printf("FAIL: %s (code %d, kerrno %d)\n",
                    rr.error.detail().c_str(),
                    rr.error.code(),
                    rr.error.kernel_errno());
        return 1;
    }
    std::printf("OK: %zu bytes read\n", rr.value);

    if (rr.value != len ||
        std::memcmp(pattern.data(), readback.data(), len) != 0) {
        std::printf("FAIL: pattern mismatch on read-back\n");
        return 1;
    }
    std::printf("OK: read-back matches written pattern\n");
    return 0;
}

}  // namespace

int main(int argc, const char* argv[]) {
    priv::backends::MacOSXpcBackend backend;
    const char* device = firstNonFlagArg(argc, argv);
    const bool allow_write = hasFlag(argc, argv, "--allow-write");

    std::printf("=== queryHelperStatus ===\n");
    auto status = backend.queryHelperStatus();
    if (!status.ok) {
        std::printf("FAIL: %s (code %d)\n",
                    status.error.detail().c_str(),
                    status.error.code());
        std::printf("\nIs the helper bootstrapped?\n"
                    "  launchctl bootstrap gui/$(id -u) "
                    "~/Library/LaunchAgents/com.raspberrypi.rpi-imager.writer.plist\n");
        return 1;
    }
    std::printf("OK: helper version=%s, client version=%s\n",
                status.value.installed_version().c_str(),
                status.value.client_version().c_str());

    std::printf("\n=== unmount /dev/disk999 (expected to fail) ===\n");
    auto unmount = backend.unmount("/dev/disk999");
    if (unmount.ok) {
        std::printf("UNEXPECTED: unmount of /dev/disk999 succeeded?\n");
        return 1;
    }
    std::printf("OK: returned structured failure - code=%d kernel_status=%d\n"
                "    detail=%s\n",
                unmount.error.code(),
                unmount.error.kernel_errno(),
                unmount.error.detail().c_str());

    // Optional: real device round-trip. Pass a /dev/diskN path as the
    // first non-flag argument (e.g. ./macos_xpc_smoke /dev/disk7) to
    // exercise openDevice + queryDeviceSize + closeSession against an
    // actual device. Add --allow-write to also write a small test
    // pattern at offset 0 and verify it reads back.
    //
    // **DESTRUCTIVE WARNING**: --allow-write overwrites the first 4 KB
    // of the device. Only use against a target you intend to wipe.
    if (device) {
        std::printf("\n=== openSession(%s) ===\n", device);
        priv::proto::OpenOptions opts;
        auto open_r = backend.openSession(device, opts);
        if (!open_r.ok) {
            std::printf("FAIL: %s (code %d, kerrno %d)\n",
                        open_r.error.detail().c_str(),
                        open_r.error.code(),
                        open_r.error.kernel_errno());
            return 1;
        }
        std::printf("OK: session id=%llu (the FD lives in the helper, never crossed XPC)\n",
                    (unsigned long long)open_r.value.v());

        std::printf("\n=== queryDeviceLimits ===\n");
        auto limits = backend.queryDeviceLimits(open_r.value);
        if (!limits.ok) {
            std::printf("FAIL: %s\n", limits.error.detail().c_str());
            (void)backend.closeSession(open_r.value);
            return 1;
        }
        std::printf("OK: device size = %llu bytes (%.2f GB)\n",
                    (unsigned long long)limits.value.total_bytes(),
                    limits.value.total_bytes() / 1e9);

        if (allow_write) {
            // Pick a sector-aligned, 4 KB region near the start. Direct
            // I/O on /dev/rdisk requires sector-aligned writes; the
            // helper opens the block /dev/disk path so this is less
            // strict, but stick to 4 KB for portability.
            int rc = doWriteVerify(backend, open_r.value,
                                    /*offset=*/0,
                                    /*len=*/4096);
            if (rc != 0) {
                (void)backend.closeSession(open_r.value);
                return rc;
            }
        } else {
            std::printf("\n(Add --allow-write to also write a 4 KB test "
                        "pattern at offset 0 and verify it reads back.)\n");
        }

        std::printf("\n=== closeSession ===\n");
        auto close_r = backend.closeSession(open_r.value);
        if (!close_r.ok) {
            std::printf("FAIL: %s\n", close_r.error.detail().c_str());
            return 1;
        }
        std::printf("OK\n");
    } else {
        std::printf("\n(Pass /dev/diskN as argv[1] to also exercise "
                    "openSession + queryDeviceLimits + closeSession.)\n");
    }

    std::printf("\nAll roundtrips completed via XPC. Boundary is alive.\n");
    return 0;
}
