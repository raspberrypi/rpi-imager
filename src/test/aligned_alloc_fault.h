/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Making an aligned allocation fail, for the refusals that depend on one.
 *
 * Only works in a target linked with
 *   -Wl,--wrap=__imp__aligned_malloc
 * and with aligned_alloc_fault.cpp. The wrap applies to everything linked
 * into that target, so it wants a binary of its own.
 */

#ifndef RPI_TEST_ALIGNED_ALLOC_FAULT_H
#define RPI_TEST_ALIGNED_ALLOC_FAULT_H

namespace rpi_test {

// Fail the allocation at `callIndex`, counting from nought, and reset the
// count. Every other allocation is passed through to the real one.
void failAlignedAllocAt(int callIndex);

// Let every allocation through again.
void stopFailingAlignedAlloc();

// How many aligned allocations have been asked for since the last arming.
int alignedAllocCallsMade();

}  // namespace rpi_test

#endif  // RPI_TEST_ALIGNED_ALLOC_FAULT_H
