/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Makes an aligned allocation fail on demand.
 *
 * The formatter checks every buffer it allocates and refuses the format if
 * one fails, but nothing could provoke that: the allocations are small and
 * succeed on any machine that can run the suite. So the refusals were
 * unreachable, and a card half-formatted because memory ran out mid-write
 * would have been nobody's tested path.
 *
 * Interception is a linker flag on this target alone. The allocation goes
 * through __imp__aligned_malloc -- a data symbol holding the address, which
 * `nm` confirms is what the object references -- so the wrap redirects a
 * pointer, and the real one is reachable through __real___imp__aligned_malloc.
 *
 * Counted rather than always-failing: a format performs several allocations
 * in sequence, and failing a chosen one says which refusal was reached.
 */

#include "aligned_alloc_fault.h"

#include <atomic>
#include <cstddef>
#include <malloc.h>

namespace {
// -1 fails nothing. Otherwise the call at this index fails, counting from
// nought, and every other call is passed through.
std::atomic<int> g_failAt{-1};
std::atomic<int> g_calls{0};
}  // namespace

namespace rpi_test {

void failAlignedAllocAt(int callIndex)
{
    g_failAt.store(callIndex);
    g_calls.store(0);
}

void stopFailingAlignedAlloc()
{
    g_failAt.store(-1);
}

int alignedAllocCallsMade()
{
    return g_calls.load();
}

}  // namespace rpi_test

extern "C" {

// Filled in by the linker with the address of the real import.
extern void *(*__real___imp__aligned_malloc)(std::size_t size, std::size_t alignment);

void *(*__wrap___imp__aligned_malloc)(std::size_t, std::size_t) = nullptr;

// The function the wrapped pointer refers to.
static void *faultingAlignedMalloc(std::size_t size, std::size_t alignment)
{
    const int index = g_calls.fetch_add(1);
    if (index == g_failAt.load())
        return nullptr;
    return __real___imp__aligned_malloc(size, alignment);
}

// Set before main, so a test never sees the null above.
namespace {
struct Installer {
    Installer() { __wrap___imp__aligned_malloc = faultingAlignedMalloc; }
};
Installer g_installer;
}  // namespace

}  // extern "C"
