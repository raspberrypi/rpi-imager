/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The embedded backend's own buffers, as the kernel sees them.
 *
 * The embedded build writes as root with O_DIRECT, and O_DIRECT refuses a
 * buffer that is not block-aligned with EINVAL. prepareDevice() and
 * hashDeviceSha256() used plain 1 MB std::vectors, which glibc places 16
 * bytes past a page boundary -- so on a Pi every write failed at "zero first
 * MB" before any of the image was written.
 *
 * O_DIRECT itself cannot be exercised here: the backend only opens /dev/*
 * paths, an unprivileged run can reach no block device, and before Linux 6.6
 * tmpfs refuses O_DIRECT outright. So pwrite and pread are wrapped at link
 * time (this target only) and the buffer each is handed is recorded; the
 * session itself is a buffered file under /dev/shm.
 */

#include <catch2/catch_test_macros.hpp>

#include "backends/linux_embedded.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

std::mutex g_mutex;
// Non-zero: the sector size BLKSSZGET reports, as a 4Kn drive would.
std::atomic<int> g_fakeSectorSize{0};
std::vector<std::uintptr_t> g_buffers;
std::vector<std::size_t> g_readLengths;

void record(const void* buf)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_buffers.push_back(reinterpret_cast<std::uintptr_t>(buf));
}

std::vector<std::uintptr_t> takeBuffers()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    return std::exchange(g_buffers, {});
}

// Page alignment satisfies O_DIRECT on any logical block size a card or
// drive reports, 512 or 4096.
bool pageAligned(std::uintptr_t p)
{
    return p % static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE)) == 0;
}

// A device-sized file the backend will accept as a /dev/* path.
struct ShmDevice {
    std::string path;
    explicit ShmDevice(off_t size)
    {
        char tmpl[] = "/dev/shm/rpi-imager-embedded-XXXXXX";
        const int fd = ::mkstemp(tmpl);
        if (fd < 0)
            return;
        if (::ftruncate(fd, size) == 0)
            path = tmpl;
        ::close(fd);
    }
    ~ShmDevice()
    {
        if (!path.empty())
            ::unlink(path.c_str());
    }
};

}  // namespace

extern "C" {
ssize_t __real_pwrite(int, const void*, size_t, off_t);
ssize_t __real_pwrite64(int, const void*, size_t, off64_t);
ssize_t __real_pread(int, void*, size_t, off_t);
ssize_t __real_pread64(int, void*, size_t, off64_t);
int __real_ioctl(int, unsigned long, ...);

// Every ioctl the backend makes takes a single pointer argument.
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    if (request == BLKSSZGET && g_fakeSectorSize.load() > 0) {
        *static_cast<int*>(arg) = g_fakeSectorSize.load();
        return 0;
    }
    return __real_ioctl(fd, request, arg);
}

ssize_t __wrap_pwrite(int fd, const void* buf, size_t n, off_t off)
{
    record(buf);
    return __real_pwrite(fd, buf, n, off);
}
ssize_t __wrap_pwrite64(int fd, const void* buf, size_t n, off64_t off)
{
    record(buf);
    return __real_pwrite64(fd, buf, n, off);
}
ssize_t __wrap_pread(int fd, void* buf, size_t n, off_t off)
{
    record(buf);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_readLengths.push_back(n);
    }
    return __real_pread(fd, buf, n, off);
}
ssize_t __wrap_pread64(int fd, void* buf, size_t n, off64_t off)
{
    record(buf);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_readLengths.push_back(n);
    }
    return __real_pread64(fd, buf, n, off);
}
}

using rpi_imager::privileged::backends::LinuxEmbeddedBackend;
namespace proto_ns = rpi_imager::privileged::proto;

TEST_CASE("Zeroing the ends of a card hands the kernel an aligned buffer",
          "[privileged][embedded]")
{
    ShmDevice dev(8 * 1024 * 1024);
    if (dev.path.empty())
        SKIP("no writable /dev/shm");

    LinuxEmbeddedBackend backend;
    const auto sid = backend.openSession(dev.path, proto_ns::OpenOptions{});
    REQUIRE(sid.ok);

    proto_ns::PrepareOptions opts;
    opts.set_zero_first_mb(true);
    opts.set_zero_last_mb(true);
    takeBuffers();
    const auto r = backend.prepareDevice(sid.value, opts);
    const auto buffers = takeBuffers();

    INFO(r.error.detail());
    REQUIRE(r.ok);
    REQUIRE(buffers.size() == 2);
    for (const auto b : buffers)
        CHECK(pageAligned(b));

    backend.closeSession(sid.value);
}

TEST_CASE("Hashing what was written reads into an aligned buffer, and gets it right",
          "[privileged][embedded]")
{
    ShmDevice dev(2 * 1024 * 1024);
    if (dev.path.empty())
        SKIP("no writable /dev/shm");

    LinuxEmbeddedBackend backend;
    const auto sid = backend.openSession(dev.path, proto_ns::OpenOptions{});
    REQUIRE(sid.ok);

    // A freshly truncated file reads back as zeros, so this is the SHA-256
    // of 1 MiB of zero bytes.
    takeBuffers();
    const auto h = backend.hashDeviceSha256(sid.value, {}, 0, 1024 * 1024);
    const auto buffers = takeBuffers();

    INFO(h.error.detail());
    REQUIRE(h.ok);
    REQUIRE_FALSE(buffers.empty());
    for (const auto b : buffers)
        CHECK(pageAligned(b));

    std::string hex;
    for (unsigned char c : h.value) {
        static const char* digits = "0123456789abcdef";
        hex += digits[c >> 4];
        hex += digits[c & 0xf];
    }
    CHECK(hex == "30e14955ebf1352266dc2ff8067e68104607e750abb9d3b36582b8af909fcb58");

    backend.closeSession(sid.value);
}

TEST_CASE("The block size reported is the one the drive has",
          "[privileged][embedded]")
{
    ShmDevice dev(1024 * 1024);
    if (dev.path.empty())
        SKIP("no writable /dev/shm");

    LinuxEmbeddedBackend backend;
    const auto sid = backend.openSession(dev.path, proto_ns::OpenOptions{});
    REQUIRE(sid.ok);

    g_fakeSectorSize = 4096;
    const auto lim = backend.queryDeviceLimits(sid.value);
    g_fakeSectorSize = 0;

    REQUIRE(lim.ok);
    CHECK(lim.value.logical_block_size() == 4096);
    backend.closeSession(sid.value);
}

TEST_CASE("Something with no sector size is taken to have 512-byte blocks",
          "[privileged][embedded]")
{
    // A regular file answers BLKSSZGET with ENOTTY.
    ShmDevice dev(1024 * 1024);
    if (dev.path.empty())
        SKIP("no writable /dev/shm");

    LinuxEmbeddedBackend backend;
    const auto sid = backend.openSession(dev.path, proto_ns::OpenOptions{});
    REQUIRE(sid.ok);

    const auto lim = backend.queryDeviceLimits(sid.value);
    REQUIRE(lim.ok);
    CHECK(lim.value.logical_block_size() == 512);
    backend.closeSession(sid.value);
}

TEST_CASE("Hashing a length short of a whole block reads the block and hashes the length",
          "[privileged][embedded]")
{
    // The verify asks for the source's length, which stops short of the
    // padding that filled the last block. O_DIRECT reads only whole blocks.
    ShmDevice dev(1024 * 1024);
    if (dev.path.empty())
        SKIP("no writable /dev/shm");

    LinuxEmbeddedBackend backend;
    const auto sid = backend.openSession(dev.path, proto_ns::OpenOptions{});
    REQUIRE(sid.ok);

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_readLengths.clear();
    }
    g_fakeSectorSize = 4096;
    const auto h = backend.hashDeviceSha256(sid.value, {}, 0, 5000);
    g_fakeSectorSize = 0;

    INFO(h.error.detail());
    REQUIRE(h.ok);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        REQUIRE_FALSE(g_readLengths.empty());
        for (const auto n : g_readLengths)
            CHECK(n % 4096 == 0);
    }

    std::string hex;
    for (unsigned char c : h.value) {
        static const char* digits = "0123456789abcdef";
        hex += digits[c >> 4];
        hex += digits[c & 0xf];
    }
    // SHA-256 of 5000 zero bytes: the length asked for, not the 8192 read.
    CHECK(hex == "7ca5bd879f393d9dd05b14f38add9c0fc6b67928f7f2d261b2e47a32ee8219e3");

    backend.closeSession(sid.value);
}
