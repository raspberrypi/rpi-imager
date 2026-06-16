// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "privileged_helper_diagnostic.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef __APPLE__
#include "privileged_io/privileged_writer.h"
#include "privileged_io/backends/macos_xpc.h"
#endif

namespace rpi_imager {

#ifdef __APPLE__

namespace {

namespace priv = ::rpi_imager::privileged;

int doWriteVerify(priv::IPrivilegedWriter& backend,
                   const priv::proto::SessionId& sid,
                   std::uint64_t offset,
                   std::size_t len) {
    std::vector<std::byte> pattern(len);
    constexpr char kSig[] = "RPI-IMAGER-TEST-";
    constexpr std::size_t kSigLen = sizeof(kSig) - 1;
    for (std::size_t i = 0; i < len; ++i) {
        pattern[i] = static_cast<std::byte>(kSig[i % kSigLen]);
    }
    if (len >= 16) {
        char stamp[17] = {0};
        std::snprintf(stamp, sizeof(stamp), "%016llx",
                      (unsigned long long)offset);
        for (std::size_t i = 0; i < 16; ++i) {
            pattern[len - 16 + i] = static_cast<std::byte>(stamp[i]);
        }
    }

    std::printf("[diagnostic] writing %zu bytes at offset %llu...\n",
                len, (unsigned long long)offset);
    auto wr = backend.writeChunk(sid, offset, pattern.data(), pattern.size());
    if (!wr.ok) {
        std::fprintf(stderr, "FAIL  writeChunk: %s (code %d, kerrno %d)\n",
                     wr.error.detail().c_str(),
                     wr.error.code(), wr.error.kernel_errno());
        return 1;
    }
    std::printf("[diagnostic]   %zu bytes written\n", wr.value);

    std::printf("[diagnostic] reading back %zu bytes...\n", len);
    std::vector<std::byte> readback(len, std::byte{0});
    auto rr = backend.readChunk(sid, offset, readback.data(), readback.size());
    if (!rr.ok) {
        std::fprintf(stderr, "FAIL  readChunk: %s (code %d)\n",
                     rr.error.detail().c_str(), rr.error.code());
        return 1;
    }
    if (rr.value != len ||
        std::memcmp(pattern.data(), readback.data(), len) != 0) {
        std::fprintf(stderr, "FAIL  pattern mismatch on read-back "
                             "(read=%zu expected=%zu)\n",
                     rr.value, len);
        return 1;
    }
    std::printf("[diagnostic]   read-back matches\n");
    return 0;
}

} // namespace

// Exercise the shared-memory bulk write path: allocate a 1 MB buffer,
// fill the first 4 KB of it with a known pattern, submit a
// bulkWriteFromBuffer to write that 4 KB to device offset 0, then read
// back via readChunk and verify. Releases the buffer at the end.
int doBulkRoundTrip(priv::backends::MacOSXpcBackend& backend,
                     const priv::proto::SessionId& sid) {
    constexpr std::size_t kBufSize = 1024 * 1024;   // 1 MB ring buffer
    constexpr std::size_t kWriteLen = 4096;
    constexpr std::uint64_t kSlotOffset = 0;
    constexpr std::uint64_t kDeviceOffset = 0;

    std::printf("[diagnostic]   mapBulkBuffer(size=%zu)...\n", kBufSize);
    auto map_r = backend.mapBulkBuffer(sid, kBufSize);
    if (!map_r.ok) {
        std::fprintf(stderr, "FAIL  mapBulkBuffer: %s (code %d)\n",
                     map_r.error.detail().c_str(), map_r.error.code());
        return 1;
    }

    void* base = backend.bulkBufferBase();
    if (!base) {
        std::fprintf(stderr, "FAIL  bulkBufferBase() returned null after map\n");
        return 1;
    }
    std::printf("[diagnostic]   shared bulk buffer mapped at %p (%zu bytes) - this is the\n"
                "[diagnostic]   same shared-memory plane the production zero-copy write path uses\n",
                base, kBufSize);

    // Fill the first 4 KB of the shared buffer with a recognisable
    // pattern. The pattern stamps offset/len in the trailer so we can
    // distinguish bulk-path output from synchronous-path output.
    std::vector<std::uint8_t> expected(kWriteLen);
    constexpr char kSig[] = "RPI-IMAGER-BULK-";
    constexpr std::size_t kSigLen = sizeof(kSig) - 1;
    auto* dst = static_cast<std::uint8_t*>(base);
    for (std::size_t i = 0; i < kWriteLen; ++i) {
        std::uint8_t byte = static_cast<std::uint8_t>(kSig[i % kSigLen]);
        expected[i] = byte;
        dst[i] = byte;
    }

    std::printf("[diagnostic]   bulkWriteFromBuffer (4 KB, helper pwrites from "
                "shared mem - no inline data)...\n");
    auto bw = backend.bulkWriteFromBuffer(sid, kSlotOffset, kDeviceOffset,
                                            kWriteLen);
    if (!bw.ok) {
        std::fprintf(stderr, "FAIL  bulkWriteFromBuffer: %s (code %d, kerrno %d)\n",
                     bw.error.detail().c_str(), bw.error.code(),
                     bw.error.kernel_errno());
        (void)backend.mapBulkBuffer(sid, 0);
        return 1;
    }
    if (bw.value != kWriteLen) {
        std::fprintf(stderr, "FAIL  bulkWriteFromBuffer: wrote %zu, expected %zu\n",
                     bw.value, kWriteLen);
        (void)backend.mapBulkBuffer(sid, 0);
        return 1;
    }
    std::printf("[diagnostic]   wrote %zu bytes via shared memory\n", bw.value);

    std::printf("[diagnostic]   readChunk verify...\n");
    std::vector<std::byte> readback(kWriteLen, std::byte{0});
    auto rr = backend.readChunk(sid, kDeviceOffset, readback.data(),
                                  readback.size());
    if (!rr.ok || rr.value != kWriteLen) {
        std::fprintf(stderr, "FAIL  readChunk: %s (got %zu)\n",
                     rr.error.detail().c_str(), rr.value);
        (void)backend.mapBulkBuffer(sid, 0);
        return 1;
    }
    if (std::memcmp(expected.data(), readback.data(), kWriteLen) != 0) {
        std::fprintf(stderr, "FAIL  bulk-path read-back mismatch\n");
        (void)backend.mapBulkBuffer(sid, 0);
        return 1;
    }
    std::printf("[diagnostic]   read-back matches - zero-copy shared-memory write plane OK\n");

    std::printf("[diagnostic]   mapBulkBuffer(size=0) release...\n");
    auto rel = backend.mapBulkBuffer(sid, 0);
    if (!rel.ok) {
        std::fprintf(stderr, "WARN  release returned failure: %s\n",
                     rel.error.detail().c_str());
    }
    return 0;
}

int runPrivilegedHelperDiagnostic(const std::string& device_path,
                                    bool allow_write,
                                    bool exercise_bulk) {
    if (device_path.empty()) {
        std::fprintf(stderr,
            "ERROR  --test-privileged-helper requires a device path\n"
            "       e.g. --test-privileged-helper /dev/disk7\n");
        return 2;
    }

    std::printf("==============================================================\n");
    std::printf(" Raspberry Pi Imager - privileged helper diagnostic\n");
    std::printf("==============================================================\n");
    std::printf(" Device: %s\n", device_path.c_str());
    std::printf(" Allow write: %s\n", allow_write ? "yes" : "no (read-only)");
    std::printf("\n");

    priv::backends::MacOSXpcBackend backend;

    // Step 1: install (or confirm installed) the helper via SMAppService.
    // First time on a given machine, this surfaces a "Login Items" prompt;
    // subsequent runs are silent.
    std::printf("[1/5] installHelper (SMAppService.register())\n");
    auto install = backend.installHelper();
    if (!install.ok) {
        std::fprintf(stderr,
            "FAIL  helper installation failed: %s (code %d)\n\n"
            "Common causes:\n"
            "  * App is not running from /Applications (move it there first).\n"
            "  * Helper or app is not signed with the Raspberry Pi Developer ID.\n"
            "  * The user declined the 'Allow in Background?' prompt.\n",
            install.error.detail().c_str(), install.error.code());
        return 1;
    }
    std::printf("OK    helper installed / already registered\n");

    // Step 2: handshake to verify the helper is actually reachable.
    std::printf("\n[2/5] queryHelperStatus (XPC ping)\n");
    auto status = backend.queryHelperStatus();
    if (!status.ok) {
        std::fprintf(stderr,
            "FAIL  ping failed: %s (code %d)\n\n"
            "If you just approved the prompt, give launchd a moment and re-run.\n",
            status.error.detail().c_str(), status.error.code());
        return 1;
    }
    std::printf("OK    helper version=%s\n",
                status.value.installed_version().c_str());

    // Step 3: openSession - the helper opens the device with its own FD.
    std::printf("\n[3/5] openSession(%s)  [helper holds the FD]\n",
                device_path.c_str());
    priv::proto::OpenOptions opts;
    auto open = backend.openSession(device_path, opts);
    if (!open.ok) {
        std::fprintf(stderr,
            "FAIL  openSession: %s (code %d, kerrno %d)\n",
            open.error.detail().c_str(),
            open.error.code(), open.error.kernel_errno());
        return 1;
    }
    std::printf("OK    session id=%llu\n",
                (unsigned long long)open.value.v());

    // Step 4: queryDeviceLimits - proves the FD is alive and works.
    std::printf("\n[4/5] queryDeviceLimits\n");
    auto limits = backend.queryDeviceLimits(open.value);
    if (!limits.ok) {
        std::fprintf(stderr,
            "FAIL  queryDeviceLimits: %s (code %d)\n",
            limits.error.detail().c_str(), limits.error.code());
        (void)backend.closeSession(open.value);
        return 1;
    }
    const auto bytes = limits.value.total_bytes();
    std::printf("OK    device size = %llu bytes (%.2f GB)\n",
                (unsigned long long)bytes, bytes / 1e9);

    // Step 5 (optional): write a small pattern + verify.
    int rc = 0;
    if (allow_write) {
        std::printf("\n[5/6] write-then-read verify (synchronous path)\n");
        rc = doWriteVerify(backend, open.value,
                            /*offset=*/0,
                            /*len=*/4096);
    } else {
        std::printf("\n[5/6] write-then-read SKIPPED "
                    "(re-run with --test-privileged-helper-allow-write to test)\n");
    }

    // Step 6 (optional): exercise the bulk shared-memory path. Same
    // device offset as the synchronous path so a successful run leaves
    // a recognisable bulk-path pattern on disk (RPI-IMAGER-BULK-...)
    // that subsequent inspections can verify.
    if (rc == 0 && allow_write && exercise_bulk) {
        std::printf("\n[6/6] bulk-write shared-memory path\n");
        rc = doBulkRoundTrip(backend, open.value);
    } else if (exercise_bulk) {
        std::printf("\n[6/6] bulk-write SKIPPED "
                    "(--test-privileged-helper-bulk requires --allow-write too)\n");
    } else {
        std::printf("\n[6/6] bulk-write SKIPPED "
                    "(re-run with --test-privileged-helper-bulk to test)\n");
    }

    std::printf("\n[7/6] closeSession\n");
    auto close_r = backend.closeSession(open.value);
    if (!close_r.ok) {
        std::fprintf(stderr, "WARN  closeSession returned failure: %s\n",
                     close_r.error.detail().c_str());
    } else {
        std::printf("OK\n");
    }

    if (rc == 0) {
        const bool bulk_exercised = allow_write && exercise_bulk;
        std::printf("\n==============================================================\n");
        std::printf(" PASS  privileged helper architecture is functional\n");
        std::printf("       zero-copy shared-memory plane: %s\n",
                    bulk_exercised ? "exercised OK"
                                   : "not exercised (add --test-privileged-helper-bulk)");
        std::printf("==============================================================\n");
    }
    return rc;
}

#else  // not __APPLE__

int runPrivilegedHelperDiagnostic(const std::string&, bool) {
    std::fprintf(stderr,
        "--test-privileged-helper is currently macOS-only "
        "(Linux/Windows backends are phases 2 and 3).\n");
    return 0;
}

#endif

} // namespace rpi_imager
