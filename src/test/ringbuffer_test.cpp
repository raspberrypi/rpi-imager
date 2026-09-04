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

#include <QElapsedTimer>

#include <atomic>
#include <chrono>
#include <thread>

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

// ── Blocking, cancellation and completion ───────────────────────────────────
//
// The slot pool is the handoff between the thread pulling the image down and
// the thread writing it to the card. The failure modes here are not corruption
// but two things a user notices immediately: a write that stops making
// progress and never returns, and a Cancel button that does nothing because
// the thread it should stop is parked waiting for a slot that will never come.

TEST_CASE("Acquiring a write slot gives up rather than hanging when full",
          "[ringbuffer]") {
    RingBuffer rb(2, 4096);

    std::vector<RingBuffer::Slot*> held;
    for (int i = 0; i < 2; ++i) {
        RingBuffer::Slot* s = rb.acquireWriteSlot(100);
        REQUIRE(s != nullptr);
        held.push_back(s);
    }

    // Pool exhausted. The timeout has to be honoured; blocking here with the
    // consumer stuck is how a write freezes with the progress bar part-filled.
    QElapsedTimer t;
    t.start();
    CHECK(rb.acquireWriteSlot(150) == nullptr);
    CHECK(t.elapsed() >= 100);
}

TEST_CASE("Acquiring a read slot gives up rather than hanging when empty",
          "[ringbuffer]") {
    RingBuffer rb(2, 4096);

    QElapsedTimer t;
    t.start();
    CHECK(rb.acquireReadSlot(150) == nullptr);
    CHECK(t.elapsed() >= 100);
}

TEST_CASE("Cancelling wakes a consumer waiting for data", "[ringbuffer]") {
    // The user pressed Cancel. A consumer parked on an empty buffer must come
    // back rather than sit there until the stall timeout fires.
    RingBuffer rb(2, 4096);

    std::atomic<bool> returned{false};
    std::thread consumer([&] {
        rb.acquireReadSlot(30000);   // would park for 30s without the cancel
        returned = true;
    });

    // Give the consumer time to actually block before cancelling.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    rb.cancel();
    consumer.join();

    CHECK(returned.load());
    CHECK(rb.isCancelled());
}

TEST_CASE("Cancelling wakes a producer waiting for a free slot", "[ringbuffer]") {
    RingBuffer rb(1, 4096);

    RingBuffer::Slot* held = rb.acquireWriteSlot(100);
    REQUIRE(held != nullptr);

    std::atomic<bool> returned{false};
    std::thread producer([&] {
        rb.acquireWriteSlot(30000);
        returned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    rb.cancel();
    producer.join();

    CHECK(returned.load());
}

TEST_CASE("A cancelled buffer keeps refusing slots", "[ringbuffer]") {
    // Once cancelled it must stay cancelled, or a racing producer can push
    // more data into a write that is being torn down.
    RingBuffer rb(2, 4096);
    rb.cancel();

    CHECK(rb.isCancelled());
    CHECK(rb.acquireWriteSlot(50) == nullptr);
    CHECK(rb.acquireReadSlot(50) == nullptr);
}

TEST_CASE("The buffer is not complete while data is still queued",
          "[ringbuffer]") {
    // Reporting completion early truncates the image: the consumer stops and
    // whatever was still in the pool never reaches the card.
    RingBuffer rb(2, 4096);

    RingBuffer::Slot* s = rb.acquireWriteSlot(100);
    REQUIRE(s != nullptr);
    rb.commitWriteSlot(s, 512);

    rb.producerDone();
    CHECK_FALSE(rb.isComplete());   // one committed slot still unread

    RingBuffer::Slot* r = rb.acquireReadSlot(100);
    REQUIRE(r != nullptr);
    CHECK(r->size == 512);
    rb.releaseReadSlot(r);

    CHECK(rb.isComplete());
}

TEST_CASE("A consumer waiting on a finished producer is released",
          "[ringbuffer]") {
    // End of the download with an empty pool: the consumer must be told there
    // is nothing more coming, not left waiting for a slot.
    RingBuffer rb(2, 4096);

    std::atomic<bool> returned{false};
    std::thread consumer([&] {
        rb.acquireReadSlot(30000);
        returned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    rb.producerDone();
    consumer.join();

    CHECK(returned.load());
    CHECK(rb.isComplete());
}

TEST_CASE("Stalls are counted so a slow side can be identified", "[ringbuffer]") {
    // These counters are what distinguishes "the network is slow" from "the
    // card is slow" in a bug report about a slow write.
    RingBuffer rb(1, 4096);

    uint64_t producerStalls = 0, consumerStalls = 0;
    uint64_t producerWaitMs = 0, consumerWaitMs = 0;

    // Consumer stall: nothing to read.
    CHECK(rb.acquireReadSlot(120) == nullptr);

    // Producer stall: pool exhausted and nothing draining it.
    RingBuffer::Slot* held = rb.acquireWriteSlot(100);
    REQUIRE(held != nullptr);
    CHECK(rb.acquireWriteSlot(120) == nullptr);

    rb.getStarvationStats(producerStalls, consumerStalls,
                          producerWaitMs, consumerWaitMs);
    INFO("producer=" << producerStalls << "/" << producerWaitMs << "ms"
         << " consumer=" << consumerStalls << "/" << consumerWaitMs << "ms");
    CHECK(producerStalls >= 1);
    CHECK(consumerStalls >= 1);
}

TEST_CASE("reset() makes a cancelled buffer usable again", "[ringbuffer]") {
    RingBuffer rb(2, 4096);
    rb.cancel();
    REQUIRE(rb.isCancelled());

    rb.reset();

    CHECK_FALSE(rb.isCancelled());
    CHECK_FALSE(rb.isComplete());
    RingBuffer::Slot* s = rb.acquireWriteSlot(100);
    CHECK(s != nullptr);
}

TEST_CASE("Slot capacity and count are reported as constructed", "[ringbuffer]") {
    RingBuffer rb(6, 8192);
    CHECK(rb.numSlots() == 6);
    CHECK(rb.slotCapacity() == 8192);
}
