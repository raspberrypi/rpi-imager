/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Tests for RingBuffer slot recycling.
 *
 * The risk being covered is silent corruption. The write path uses the ring
 * buffer as a slot pool for zero-copy asynchronous I/O: a slot is handed to the
 * kernel and only released when the write completes, and those completions
 * arrive out of order. If a release frees "a slot" rather than "that slot", the
 * producer can be handed a buffer the kernel is still reading from, and the
 * decompressor overwrites data mid-flight. Nothing reports an error -- it
 * surfaces only as a post-write verification hash mismatch. See #1598.
 */

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <set>
#include <vector>

#include "ringbuffer.h"

TEST_CASE("Slots are recycled by identity, not by count", "[ringbuffer]") {
    RingBuffer rb(4, 4096);

    std::vector<RingBuffer::Slot*> held;
    for (int i = 0; i < 4; ++i) {
        RingBuffer::Slot* slot = rb.acquireWriteSlot(100);
        REQUIRE(slot != nullptr);
        held.push_back(slot);
    }

    SECTION("an exhausted pool hands out nothing") {
        REQUIRE(rb.acquireWriteSlot(50) == nullptr);
    }

    SECTION("an out-of-order release recycles the slot that was released") {
        rb.releaseReadSlot(held[2]);
        REQUIRE(rb.acquireWriteSlot(100) == held[2]);
        // The other three are still in use and must not be handed out.
        REQUIRE(rb.acquireWriteSlot(50) == nullptr);
    }

    SECTION("releases in reverse order each recycle their own slot") {
        for (int i = 3; i >= 0; --i) {
            rb.releaseReadSlot(held[i]);
            REQUIRE(rb.acquireWriteSlot(100) == held[i]);
        }
    }

    SECTION("releasing a slot twice does not duplicate it") {
        rb.releaseReadSlot(held[0]);
        rb.releaseReadSlot(held[0]);
        REQUIRE(rb.acquireWriteSlot(100) == held[0]);
        REQUIRE(rb.acquireWriteSlot(50) == nullptr);
    }
}

TEST_CASE("No slot is handed out while still in use", "[ringbuffer]") {
    // Simulates the write path: keep a bounded number of "writes" outstanding
    // and complete them in a shuffled order, asserting that every slot handed
    // out is one nothing else holds.
    constexpr std::size_t kSlots = 8;
    constexpr int kQueueDepth = 6;
    RingBuffer rb(kSlots, 4096);

    std::set<RingBuffer::Slot*> inFlight;
    std::vector<RingBuffer::Slot*> pending;
    std::mt19937 rng(1598);  // fixed seed: deterministic in CI

    for (int i = 0; i < 2000; ++i) {
        if (static_cast<int>(pending.size()) >= kQueueDepth) {
            // Complete one outstanding write, chosen out of order.
            std::size_t victim = rng() % pending.size();
            RingBuffer::Slot* done = pending[victim];
            pending.erase(pending.begin() + static_cast<long>(victim));
            inFlight.erase(done);
            rb.releaseReadSlot(done);
        }

        RingBuffer::Slot* slot = rb.acquireWriteSlot(100);
        REQUIRE(slot != nullptr);
        REQUIRE(inFlight.count(slot) == 0);  // never a buffer still being read
        inFlight.insert(slot);
        pending.push_back(slot);
    }
}

TEST_CASE("Committed slots are read in commit order", "[ringbuffer]") {
    RingBuffer rb(3, 4096);

    std::vector<RingBuffer::Slot*> committed;
    for (std::size_t i = 0; i < 3; ++i) {
        RingBuffer::Slot* slot = rb.acquireWriteSlot(100);
        REQUIRE(slot != nullptr);
        rb.commitWriteSlot(slot, 100 + i);
        committed.push_back(slot);
    }

    for (std::size_t i = 0; i < 3; ++i) {
        RingBuffer::Slot* slot = rb.acquireReadSlot(100);
        REQUIRE(slot == committed[i]);
        REQUIRE(slot->size == 100 + i);
    }

    // Recycling out of order must not disturb the order the consumer sees:
    // slot indices are no longer allocated in rotation, so commit order is
    // tracked explicitly.
    rb.releaseReadSlot(committed[1]);
    rb.releaseReadSlot(committed[0]);
    RingBuffer::Slot* first = rb.acquireWriteSlot(100);
    RingBuffer::Slot* second = rb.acquireWriteSlot(100);
    REQUIRE(first == committed[0]);   // most recently released is reused first
    REQUIRE(second == committed[1]);

    rb.commitWriteSlot(first, 7);
    rb.commitWriteSlot(second, 9);
    RingBuffer::Slot* read1 = rb.acquireReadSlot(100);
    RingBuffer::Slot* read2 = rb.acquireReadSlot(100);
    REQUIRE(read1 == first);
    REQUIRE(read1->size == 7);
    REQUIRE(read2 == second);
    REQUIRE(read2->size == 9);
}

TEST_CASE("reset() returns every slot to the pool", "[ringbuffer]") {
    RingBuffer rb(3, 4096);

    RingBuffer::Slot* first = rb.acquireWriteSlot(100);
    REQUIRE(rb.acquireWriteSlot(100) != nullptr);
    REQUIRE(rb.acquireWriteSlot(100) != nullptr);
    REQUIRE(rb.acquireWriteSlot(50) == nullptr);

    rb.reset();

    REQUIRE(rb.acquireWriteSlot(100) == first);
    REQUIRE(rb.acquireWriteSlot(100) != nullptr);
    REQUIRE(rb.acquireWriteSlot(100) != nullptr);
    REQUIRE(rb.acquireWriteSlot(50) == nullptr);
}
