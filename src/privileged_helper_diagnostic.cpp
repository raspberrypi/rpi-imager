// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "privileged_helper_diagnostic.h"

#include "privileged_io/privileged_writer.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include "privileged_io/backends/macos_xpc.h"
#endif

#ifdef _WIN32
#include "privileged_io/backends/windows_uac.h"
#include "privileged_io_glue.h"
#endif

#if defined(__linux__)
#include "privileged_io/backends/linux_polkit.h"
#include "privileged_io/backends/linux_embedded.h"
#include "privileged_io_glue.h"
#endif

namespace rpi_imager {

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

template <typename BulkBackend>
int doBulkRoundTrip(BulkBackend& backend, const priv::proto::SessionId& sid) {
    constexpr std::size_t kBufSize = 1024 * 1024;
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
    std::printf("[diagnostic]   shared bulk buffer mapped at %p (%zu bytes)\n",
                base, kBufSize);

    std::vector<std::uint8_t> expected(kWriteLen);
    constexpr char kSig[] = "RPI-IMAGER-BULK-";
    constexpr std::size_t kSigLen = sizeof(kSig) - 1;
    auto* dst = static_cast<std::uint8_t*>(base);
    for (std::size_t i = 0; i < kWriteLen; ++i) {
        const std::uint8_t byte = static_cast<std::uint8_t>(kSig[i % kSigLen]);
        expected[i] = byte;
        dst[i] = byte;
    }

    std::printf("[diagnostic]   bulkWriteFromBuffer (4 KB)...\n");
    auto bw = backend.bulkWriteFromBuffer(sid, kSlotOffset, kDeviceOffset, kWriteLen);
    if (!bw.ok || bw.value != kWriteLen) {
        std::fprintf(stderr, "FAIL  bulkWriteFromBuffer: %s\n",
                     bw.ok ? "short write" : bw.error.detail().c_str());
        (void)backend.mapBulkBuffer(sid, 0);
        return 1;
    }
    std::printf("[diagnostic]   wrote %zu bytes via shared memory\n", bw.value);

    std::vector<std::byte> readback(kWriteLen, std::byte{0});
    auto rr = backend.readChunk(sid, kDeviceOffset, readback.data(), readback.size());
    if (!rr.ok || rr.value != kWriteLen ||
        std::memcmp(expected.data(), readback.data(), kWriteLen) != 0) {
        std::fprintf(stderr, "FAIL  bulk-path read-back mismatch\n");
        (void)backend.mapBulkBuffer(sid, 0);
        return 1;
    }
    std::printf("[diagnostic]   read-back matches - shared-memory write plane OK\n");
    (void)backend.mapBulkBuffer(sid, 0);
    return 0;
}

int runDiagnosticCore(priv::IPrivilegedWriter& backend,
                      const std::string& device_path,
                      bool allow_write,
                      bool exercise_bulk,
                      const char* install_label) {
    if (device_path.empty()) {
        std::fprintf(stderr,
            "ERROR  --test-privileged-helper requires a device path\n"
            "       e.g. --test-privileged-helper /dev/sda\n");
        return 2;
    }

    std::printf("==============================================================\n");
    std::printf(" Raspberry Pi Imager - privileged helper diagnostic\n");
    std::printf("==============================================================\n");
    std::printf(" Device: %s\n", device_path.c_str());
    std::printf(" Backend: %s\n", backend.backendDescription().c_str());
    std::printf(" Allow write: %s\n", allow_write ? "yes" : "no (read-only)");
    std::printf("\n");

    std::printf("[1/5] installHelper (%s)\n", install_label);
    auto install = backend.installHelper();
    if (!install.ok) {
        std::fprintf(stderr, "FAIL  helper installation failed: %s (code %d)\n",
                     install.error.detail().c_str(), install.error.code());
        return 1;
    }
    std::printf("OK    helper reachable\n");

    std::printf("\n[2/5] queryHelperStatus\n");
    auto status = backend.queryHelperStatus();
    if (!status.ok) {
        std::fprintf(stderr, "FAIL  status query failed: %s (code %d)\n",
                     status.error.detail().c_str(), status.error.code());
        return 1;
    }
    std::printf("OK    helper version=%s\n",
                status.value.installed_version().c_str());

    std::printf("\n[3/5] openSession(%s)\n", device_path.c_str());
    priv::proto::OpenOptions opts;
    auto open = backend.openSession(device_path, opts);
    if (!open.ok) {
        std::fprintf(stderr, "FAIL  openSession: %s (code %d, kerrno %d)\n",
                     open.error.detail().c_str(),
                     open.error.code(), open.error.kernel_errno());
        return 1;
    }
    std::printf("OK    session id=%llu\n", (unsigned long long)open.value.v());

    std::printf("\n[4/5] queryDeviceLimits\n");
    auto limits = backend.queryDeviceLimits(open.value);
    if (!limits.ok) {
        std::fprintf(stderr, "FAIL  queryDeviceLimits: %s (code %d)\n",
                     limits.error.detail().c_str(), limits.error.code());
        (void)backend.closeSession(open.value);
        return 1;
    }
    const auto bytes = limits.value.total_bytes();
    std::printf("OK    device size = %llu bytes (%.2f GB)\n",
                (unsigned long long)bytes, bytes / 1e9);

    int rc = 0;
    if (allow_write) {
        std::printf("\n[5/6] write-then-read verify (synchronous path)\n");
        rc = doWriteVerify(backend, open.value, 0, 4096);
    } else {
        std::printf("\n[5/6] write-then-read SKIPPED "
                    "(re-run with --test-privileged-helper-allow-write)\n");
    }

    if (rc == 0 && allow_write && exercise_bulk) {
        std::printf("\n[6/6] bulk-write shared-memory path\n");
        bool bulk_ok = false;
#ifdef __APPLE__
        if (auto* xpc = dynamic_cast<priv::backends::MacOSXpcBackend*>(&backend)) {
            rc = doBulkRoundTrip(*xpc, open.value);
            bulk_ok = true;
        }
#endif
#if defined(__linux__)
        if (!bulk_ok) {
            if (auto* polkit = dynamic_cast<priv::backends::LinuxPolkitBackend*>(&backend)) {
                rc = doBulkRoundTrip(*polkit, open.value);
                bulk_ok = true;
            } else if (auto* embedded =
                           dynamic_cast<priv::backends::LinuxEmbeddedBackend*>(&backend)) {
                rc = doBulkRoundTrip(*embedded, open.value);
                bulk_ok = true;
            }
        }
#endif
#ifdef _WIN32
        if (!bulk_ok) {
            if (auto* uac = dynamic_cast<priv::backends::WindowsUacBackend*>(&backend)) {
                rc = doBulkRoundTrip(*uac, open.value);
                bulk_ok = true;
            }
        }
#endif
        if (!bulk_ok) {
            std::fprintf(stderr, "FAIL  bulk path not available on this backend\n");
            rc = 1;
        }
    } else if (exercise_bulk) {
        std::printf("\n[6/6] bulk-write SKIPPED (needs --test-privileged-helper-allow-write)\n");
    } else {
        std::printf("\n[6/6] bulk-write SKIPPED (add --test-privileged-helper-bulk)\n");
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
        std::printf("\n==============================================================\n");
        std::printf(" PASS  privileged helper architecture is functional\n");
        std::printf("       zero-copy shared-memory plane: %s\n",
                    (allow_write && exercise_bulk) ? "exercised OK"
                                                   : "not exercised");
        std::printf("==============================================================\n");
    }
    return rc;
}

} // namespace

#ifdef __APPLE__

int runPrivilegedHelperDiagnostic(const std::string& device_path,
                                  bool allow_write,
                                  bool exercise_bulk) {
    priv::backends::MacOSXpcBackend backend;
    return runDiagnosticCore(backend, device_path, allow_write, exercise_bulk,
                             "SMAppService.register()");
}

#elif defined(__linux__)

int runPrivilegedHelperDiagnostic(const std::string& device_path,
                                  bool allow_write,
                                  bool exercise_bulk) {
#if !defined(RPI_IMAGER_ENABLE_LINUX_HELPER)
    if (::geteuid() != 0) {
        std::fprintf(stderr,
            "Build without -DRPI_IMAGER_DISABLE_LINUX_HELPER=ON and set\n"
            "RPI_IMAGER_USE_LINUX_HELPER=1, or run as root for the embedded path.\n");
        return 2;
    }
#endif
    const char* use = std::getenv("RPI_IMAGER_USE_LINUX_HELPER");
    if (::geteuid() != 0 && !(use && use[0] == '1')) {
        std::fprintf(stderr,
            "Set RPI_IMAGER_USE_LINUX_HELPER=1 for the polkit helper path,\n"
            "or run as root for the embedded in-process path.\n");
        return 2;
    }

    auto& backend = getProcessPrivilegedWriter();
    return runDiagnosticCore(backend, device_path, allow_write, exercise_bulk,
                             "pkexec / lazy connect");
}

#elif defined(_WIN32)

int runPrivilegedHelperDiagnostic(const std::string& device_path,
                                  bool allow_write,
                                  bool exercise_bulk) {
#if !defined(RPI_IMAGER_ENABLE_WINDOWS_HELPER)
    std::fprintf(stderr,
        "Build without -DRPI_IMAGER_DISABLE_WINDOWS_HELPER=ON and set\n"
        "RPI_IMAGER_USE_WINDOWS_HELPER=1.\n");
    return 2;
#endif
    const char* use = std::getenv("RPI_IMAGER_USE_WINDOWS_HELPER");
    if (!(use && use[0] == '1')) {
        std::fprintf(stderr, "Set RPI_IMAGER_USE_WINDOWS_HELPER=1.\n");
        return 2;
    }

    auto& backend = getProcessPrivilegedWriter();
    return runDiagnosticCore(backend, device_path, allow_write, exercise_bulk,
                             "UAC elevated launch");
}

#else

int runPrivilegedHelperDiagnostic(const std::string&, bool, bool) {
    std::fprintf(stderr,
        "--test-privileged-helper: use macOS (default), Linux "
        "(RPI_IMAGER_USE_LINUX_HELPER=1), or Windows "
        "(RPI_IMAGER_USE_WINDOWS_HELPER=1).\n");
    return 2;
}

#endif

} // namespace rpi_imager
