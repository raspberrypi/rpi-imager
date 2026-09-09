// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// Fault injection for the macOS write path, by dyld interposition.
//
// macOS has no device-mapper, and needs none: AsyncWriteSequential is
// dispatch_async() around a blocking PwriteAligned(), so the failure the
// macOS branch handles *is* pwrite returning -1/EIO.
//
// Inserted via DYLD_INSERT_LIBRARIES: dyld only honours __interpose at launch.

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static _Atomic int      g_armed;
static _Atomic uint64_t g_dev;
static _Atomic uint64_t g_ino;
static _Atomic uint64_t g_total;      // 0 = unbounded
static _Atomic uint64_t g_bad_start;
static _Atomic uint64_t g_bad_len;   // 0 = nothing fails
static _Atomic uint64_t g_delay_ns;
static _Atomic uint64_t g_failed;

// By (dev, ino), not path: OpenDevice()'s fd comes from authopen, dup()ed.
// Uncached, because a recycled fd would fail the wrong file's writes.
static int fd_is_target(int fd)
{
    struct stat st;
    if (fstat(fd, &st) != 0)
        return 0;
    return (uint64_t)st.st_dev == atomic_load_explicit(&g_dev, memory_order_relaxed)
        && (uint64_t)st.st_ino == atomic_load_explicit(&g_ino, memory_order_relaxed);
}

// dm commits the good part of a split bio; this refuses the write.
static int hits_bad_band(off_t offset, size_t len)
{
    const uint64_t bad_len = atomic_load_explicit(&g_bad_len, memory_order_relaxed);
    if (bad_len == 0)
        return 0;

    const uint64_t bad_start = atomic_load_explicit(&g_bad_start, memory_order_relaxed);
    const uint64_t start = (uint64_t)offset;
    return start < bad_start + bad_len && bad_start < start + (uint64_t)len;
}

static void hold_for_delay(void)
{
    const uint64_t ns = atomic_load_explicit(&g_delay_ns, memory_order_relaxed);
    if (ns == 0)
        return;

    struct timespec ts = { .tv_sec  = (time_t)(ns / 1000000000ull),
                           .tv_nsec = (long)(ns % 1000000000ull) };
    struct timespec rem;
    while (nanosleep(&ts, &rem) != 0 && errno == EINTR)
        ts = rem;
}

static ssize_t rpi_faulty_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    if (!atomic_load_explicit(&g_armed, memory_order_acquire) || !fd_is_target(fd))
        return pwrite(fd, buf, count, offset);

    hold_for_delay();

    // A real device ends; the backing file would just grow.
    const uint64_t total = atomic_load_explicit(&g_total, memory_order_relaxed);
    if (total != 0 && (uint64_t)offset + count > total) {
        atomic_fetch_add_explicit(&g_failed, 1, memory_order_relaxed);
        errno = ENOSPC;
        return -1;
    }

    if (hits_bad_band(offset, count)) {
        atomic_fetch_add_explicit(&g_failed, 1, memory_order_relaxed);
        errno = EIO;
        return -1;
    }
    return pwrite(fd, buf, count, offset);
}

// Reads fail in the band too, so verification cannot pass.
static ssize_t rpi_faulty_pread(int fd, void *buf, size_t count, off_t offset)
{
    if (!atomic_load_explicit(&g_armed, memory_order_acquire) || !fd_is_target(fd))
        return pread(fd, buf, count, offset);

    if (hits_bad_band(offset, count)) {
        errno = EIO;
        return -1;
    }
    return pread(fd, buf, count, offset);
}

#define RPI_INTERPOSE(replacement, original)                              \
    __attribute__((used, section("__DATA,__interpose")))                  \
    static const struct { const void *r; const void *o; }                 \
        rpi_interpose_##original = { (const void *)&replacement,          \
                                     (const void *)&original }

RPI_INTERPOSE(rpi_faulty_pwrite, pwrite);
RPI_INTERPOSE(rpi_faulty_pread,  pread);

__attribute__((visibility("default")))
int rpi_faulty_arm(const char *path, uint64_t total, uint64_t bad_start,
                   uint64_t bad_len, uint64_t delay_ns)
{
    struct stat st;
    if (path == NULL || stat(path, &st) != 0)
        return -1;

    atomic_store_explicit(&g_dev, (uint64_t)st.st_dev, memory_order_relaxed);
    atomic_store_explicit(&g_ino, (uint64_t)st.st_ino, memory_order_relaxed);
    atomic_store_explicit(&g_total, total, memory_order_relaxed);
    atomic_store_explicit(&g_bad_start, bad_start, memory_order_relaxed);
    atomic_store_explicit(&g_bad_len, bad_len, memory_order_relaxed);
    atomic_store_explicit(&g_delay_ns, delay_ns, memory_order_relaxed);
    atomic_store_explicit(&g_failed, 0, memory_order_relaxed);
    atomic_store_explicit(&g_armed, 1, memory_order_release);
    return 0;
}

__attribute__((visibility("default")))
void rpi_faulty_disarm(void)
{
    atomic_store_explicit(&g_armed, 0, memory_order_release);
    atomic_store_explicit(&g_total, 0, memory_order_relaxed);
    atomic_store_explicit(&g_bad_len, 0, memory_order_relaxed);
    atomic_store_explicit(&g_delay_ns, 0, memory_order_relaxed);
}

__attribute__((visibility("default")))
uint64_t rpi_faulty_injected_failures(void)
{
    return atomic_load_explicit(&g_failed, memory_order_relaxed);
}
