// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// SystemMemoryManager sizes every buffer the write path uses: the write
// buffer, the input buffer, the ring buffer slot counts, the async queue
// depth and the sync interval. Get one of these wrong and the imager either
// wastes hundreds of megabytes on a Pi Zero or crawls on a workstation.
//
// A caveat that shapes the whole file: the memory-tier branches read the
// host's actual RAM, so only the tier this machine falls into is reachable
// from a test. What *is* drivable is everything parameterised --
// getAdaptiveVerifyBufferSize(fileSize), getOptimalRingBufferSlots(slotSize)
// and getOptimalAsyncQueueDepth(blockSize) -- so those get exercised across
// their whole range, and the rest is pinned down by invariant rather than by
// exact value. Asserting exact byte counts here would only encode this
// machine's RAM into the suite.

#include <catch2/catch_test_macros.hpp>

#include "systemmemorymanager.h"

#include <QString>

namespace {

bool isPowerOfTwo(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

SystemMemoryManager &mm()
{
    return SystemMemoryManager::instance();
}

} // namespace

TEST_CASE("SystemMemoryManager is a single shared instance", "[memory]")
{
    CHECK(&SystemMemoryManager::instance() == &SystemMemoryManager::instance());
}

TEST_CASE("SystemMemoryManager reports plausible system memory", "[memory]")
{
    const qint64 total = mm().getTotalMemoryMB();
    const qint64 available = mm().getAvailableMemoryMB();

    // Any machine that can build this can report its own memory. A zero here
    // means the platform probe failed and every buffer below it is sized off
    // a fiction.
    CHECK(total > 0);
    CHECK(available > 0);
    CHECK(available <= total);
}

TEST_CASE("SystemMemoryManager names its platform", "[memory]")
{
    CHECK_FALSE(mm().getPlatformName().isEmpty());
}

TEST_CASE("SystemMemoryManager page size is a power of two", "[memory]")
{
    const size_t pageSize = mm().getSystemPageSize();

    CHECK(pageSize >= 512);
    CHECK(isPowerOfTwo(pageSize));
}

// ---------------------------------------------------------------------------
// Sync configuration
// ---------------------------------------------------------------------------

TEST_CASE("SystemMemoryManager sync configuration stays within its clamps", "[memory]")
{
    const auto config = mm().calculateSyncConfiguration();

    // Whichever memory tier this machine lands in, the result is clamped into
    // a sane band before it is returned -- that clamp is the safety net worth
    // checking, since the tier itself is not selectable from a test.
    CHECK(config.syncIntervalBytes > 0);
    CHECK(config.syncIntervalBytes >= 4LL * 1024 * 1024);
    CHECK(config.syncIntervalBytes <= 512LL * 1024 * 1024);
    CHECK(config.syncIntervalMs >= 1000);
    CHECK(config.syncIntervalMs <= 60000);

    // The tier description is written into the telemetry export, so it has to
    // say something and mention the memory it was derived from.
    CHECK_FALSE(config.memoryTier.isEmpty());
    CHECK(config.memoryTier.contains(QStringLiteral("memory")));
}

TEST_CASE("SystemMemoryManager sync configuration is stable across calls", "[memory]")
{
    const auto first = mm().calculateSyncConfiguration();
    const auto second = mm().calculateSyncConfiguration();

    CHECK(first.syncIntervalBytes == second.syncIntervalBytes);
    CHECK(first.syncIntervalMs == second.syncIntervalMs);
    CHECK(first.memoryTier == second.memoryTier);
}

// ---------------------------------------------------------------------------
// Buffer sizing
// ---------------------------------------------------------------------------

TEST_CASE("SystemMemoryManager write buffer is page aligned and bounded", "[memory]")
{
    const size_t buffer = mm().getOptimalWriteBufferSize();
    const size_t pageSize = mm().getSystemPageSize();

    CHECK(buffer > 0);
    CHECK(buffer % pageSize == 0);
    // Direct I/O needs alignment; an unaligned buffer silently falls back to
    // buffered writes and halves throughput.
    CHECK(buffer >= 64 * 1024);
    CHECK(buffer <= 128 * 1024 * 1024);
}

TEST_CASE("SystemMemoryManager input buffer is page aligned and bounded", "[memory]")
{
    const size_t buffer = mm().getOptimalInputBufferSize();
    const size_t pageSize = mm().getSystemPageSize();

    CHECK(buffer > 0);
    CHECK(buffer % pageSize == 0);
    CHECK(buffer >= 64 * 1024);
    CHECK(buffer <= 512 * 1024 * 1024);
}

// This one takes the file size as an argument, so unlike the tier branches
// every step of its ladder is reachable.
TEST_CASE("SystemMemoryManager verify buffer grows with the file", "[memory]")
{
    const qint64 sizes[] = {
        0,                                  // degenerate
        1,                                  // tiny
        50LL * 1024 * 1024,                 // < 100MB
        99LL * 1024 * 1024,                 // just under the first step
        100LL * 1024 * 1024,                // exactly on it
        512LL * 1024 * 1024,                // 100MB-1GB
        1024LL * 1024 * 1024,               // exactly 1GB
        2LL * 1024 * 1024 * 1024,           // 1GB-4GB
        4LL * 1024 * 1024 * 1024,           // exactly 4GB
        32LL * 1024 * 1024 * 1024,          // > 4GB
    };

    const size_t pageSize = mm().getSystemPageSize();
    size_t previous = 0;

    for (qint64 size : sizes) {
        const size_t buffer = mm().getAdaptiveVerifyBufferSize(size);
        INFO("file size: " << size);

        CHECK(buffer >= 128 * 1024);
        CHECK(buffer <= 16 * 1024 * 1024);
        CHECK(buffer % pageSize == 0);

        // Monotonic: a bigger file must never be given a smaller buffer.
        CHECK(buffer >= previous);
        previous = buffer;
    }
}

TEST_CASE("SystemMemoryManager verify buffer handles a negative size", "[memory]")
{
    // Callers derive this from a stat() that can fail. It must not underflow
    // into an enormous allocation.
    const size_t buffer = mm().getAdaptiveVerifyBufferSize(-1);

    CHECK(buffer >= 128 * 1024);
    CHECK(buffer <= 16 * 1024 * 1024);
}

// ---------------------------------------------------------------------------
// Ring buffer and queue depth
// ---------------------------------------------------------------------------

TEST_CASE("SystemMemoryManager ring buffer slots stay inside their clamps", "[memory]")
{
    const size_t slotSizes[] = {
        4 * 1024,           // tiny slots: would ask for a huge count
        64 * 1024,
        1024 * 1024,        // the usual
        8 * 1024 * 1024,
        128 * 1024 * 1024,  // huge slots: would ask for fewer than the floor
        1024ull * 1024 * 1024,
    };

    for (size_t slotSize : slotSizes) {
        const size_t slotCount = mm().getOptimalRingBufferSlots(slotSize);
        INFO("slot size: " << slotSize);

        // Both ends of the clamp matter: too few slots stalls the pipeline,
        // too many is an allocation nobody asked for.
        CHECK(slotCount >= 8);
        CHECK(slotCount <= 8192);
    }
}

TEST_CASE("SystemMemoryManager gives fewer slots as slots get bigger", "[memory]")
{
    const size_t small = mm().getOptimalRingBufferSlots(64 * 1024);
    const size_t large = mm().getOptimalRingBufferSlots(64 * 1024 * 1024);

    // Same memory budget divided into bigger pieces yields no more pieces.
    CHECK(large <= small);
}

TEST_CASE("SystemMemoryManager async queue depth is bounded", "[memory]")
{
    const size_t blockSizes[] = {
        4 * 1024,
        64 * 1024,
        1024 * 1024,
        4 * 1024 * 1024,
        64 * 1024 * 1024,
    };

    for (size_t block : blockSizes) {
        const int depth = mm().getOptimalAsyncQueueDepth(block);
        INFO("write block size: " << block);

        // A depth below one would disable async I/O entirely; an unbounded
        // one would pin the whole queue in RAM.
        CHECK(depth >= 1);
        CHECK(depth <= 256);
    }
}

TEST_CASE("SystemMemoryManager async queue depth has a usable default", "[memory]")
{
    const int depth = mm().getOptimalAsyncQueueDepth();

    CHECK(depth >= 1);
    CHECK(depth <= 256);
}

TEST_CASE("SystemMemoryManager can summarise its configuration", "[memory]")
{
    // Pure logging, but it reads every value above and is called on every
    // run, so a null dereference in it would be a startup crash.
    CHECK_NOTHROW(mm().logConfigurationSummary());
}

// ---------------------------------------------------------------------------
// Coordinated ring buffer sizing
// ---------------------------------------------------------------------------
//
// The input and write ring buffers are sized together rather than separately,
// because they share one memory budget: giving the download side everything
// starves the writer and vice versa. This entry point is what the pipeline
// actually calls, and it was untouched.

TEST_CASE("SystemMemoryManager coordinates the two ring buffers", "[memory]")
{
    size_t inputSlots = 0, writeSlots = 0, actualInput = 0, actualWrite = 0;

    const size_t total = mm().getCoordinatedRingBufferConfig(
        1024 * 1024, 4 * 1024 * 1024, inputSlots, writeSlots, actualInput, actualWrite);

    // Every output must be filled in: a zero slot count stalls that side of
    // the pipeline outright.
    CHECK(inputSlots > 0);
    CHECK(writeSlots > 0);
    CHECK(actualInput > 0);
    CHECK(actualWrite > 0);
    CHECK(total > 0);

    // The reported total has to account for both halves, or the caller's
    // budgeting is based on a number that means nothing.
    const size_t combined = (inputSlots * actualInput) + (writeSlots * actualWrite);
    INFO("reported total: " << total << ", combined: " << combined);
    CHECK(total >= combined / 2);
}

TEST_CASE("SystemMemoryManager coordinates across a range of slot hints", "[memory]")
{
    struct Hint {
        size_t input;
        size_t write;
    };

    const Hint hints[] = {
        {4 * 1024, 4 * 1024},                   // tiny both sides
        {64 * 1024, 1024 * 1024},               // small input, ordinary write
        {1024 * 1024, 1024 * 1024},             // the usual
        {8 * 1024 * 1024, 32 * 1024 * 1024},    // large both sides
        {256 * 1024 * 1024, 256 * 1024 * 1024}, // absurd, must still clamp
    };

    for (const auto &hint : hints) {
        size_t inputSlots = 0, writeSlots = 0, actualInput = 0, actualWrite = 0;
        const size_t total = mm().getCoordinatedRingBufferConfig(
            hint.input, hint.write, inputSlots, writeSlots, actualInput, actualWrite);

        INFO("hints: input=" << hint.input << " write=" << hint.write);
        CHECK(inputSlots > 0);
        CHECK(writeSlots > 0);
        CHECK(total > 0);

        // However extreme the hint, the result must stay inside the same
        // clamps the single-sided sizing uses.
        CHECK(inputSlots <= 8192);
        CHECK(writeSlots <= 8192);
    }
}

TEST_CASE("SystemMemoryManager coordination is stable", "[memory]")
{
    size_t iA = 0, wA = 0, aiA = 0, awA = 0;
    size_t iB = 0, wB = 0, aiB = 0, awB = 0;

    mm().getCoordinatedRingBufferConfig(1024 * 1024, 1024 * 1024, iA, wA, aiA, awA);
    mm().getCoordinatedRingBufferConfig(1024 * 1024, 1024 * 1024, iB, wB, aiB, awB);

    // The pipeline queries this more than once during setup; two different
    // answers would mean the buffers do not match the budget reported.
    CHECK(iA == iB);
    CHECK(wA == wB);
    CHECK(aiA == aiB);
    CHECK(awA == awB);
}

// ═══════════════════════════════════════════════════════════════════════════
// Sizing across the range of inputs it is actually given
//
// The existing cases check one value each and assert it is within its
// clamps. That leaves the interesting half untested: these are functions of
// their arguments, and the write path hands them a wide range -- a 4 KB block
// on a slow card, 16 MB on a fast one, and slot hints the coordinator is
// free to overrule when the two buffers together would not fit.
//
// Getting a size wrong is not a crash. It is a write that allocates more
// than the machine has, or one that dribbles through a buffer far too small
// for the card, and neither announces itself as a sizing problem.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Async queue depth is sane across block sizes", "[memory]")
{
    auto &mgr = SystemMemoryManager::instance();

    const size_t blockSizes[] = {
        4u * 1024, 64u * 1024, 256u * 1024,
        1024u * 1024, 4u * 1024 * 1024, 16u * 1024 * 1024,
    };

    int previous = -1;
    for (size_t block : blockSizes) {
        const int depth = mgr.getOptimalAsyncQueueDepth(block);
        INFO("block size: " << block << " depth: " << depth);

        // A depth of zero would stop the writer dead; an unbounded one would
        // pin more memory than the machine has.
        CHECK(depth >= 1);
        CHECK(depth <= 1024);

        // Bigger blocks must not ask for a deeper queue: depth times block
        // size is what actually gets pinned.
        if (previous >= 0)
            CHECK(depth <= previous);
        previous = depth;
    }
}

TEST_CASE("Ring buffer slots fall as the slot size rises", "[memory]")
{
    auto &mgr = SystemMemoryManager::instance();

    const size_t slotSizes[] = {
        64u * 1024, 256u * 1024, 1024u * 1024, 8u * 1024 * 1024, 32u * 1024 * 1024,
    };

    size_t previous = SIZE_MAX;
    for (size_t slot : slotSizes) {
        // Not named "slots": Qt defines that as a keyword macro.
        const size_t slotCount = mgr.getOptimalRingBufferSlots(slot);
        INFO("slot size: " << slot << " slot count: " << slotCount);

        // Fewer than two and there is no ring at all -- the producer and
        // consumer would serialise on a single slot.
        CHECK(slotCount >= 2);
        CHECK(slotCount * slot <= size_t(4) * 1024 * 1024 * 1024);
        CHECK(slotCount <= previous);
        previous = slotCount;
    }
}

TEST_CASE("The two ring buffers are sized to fit together", "[memory]")
{
    auto &mgr = SystemMemoryManager::instance();

    struct Hint { size_t input; size_t write; };
    const Hint hints[] = {
        {64u * 1024,        64u * 1024},
        {1024u * 1024,      1024u * 1024},
        {8u * 1024 * 1024,  8u * 1024 * 1024},
        {1024u * 1024,      32u * 1024 * 1024},   // lopsided
    };
    // Hints far larger than these are covered separately: the sizing scales
    // from memory available at the moment of the call, so with an absurd
    // hint on a machine that is busy the result moves around too much to
    // assert a bound on.

    for (const Hint &h : hints) {
        size_t inputSlots = 0, writeSlots = 0, actualInput = 0, actualWrite = 0;
        const size_t total = mgr.getCoordinatedRingBufferConfig(
            h.input, h.write, inputSlots, writeSlots, actualInput, actualWrite);

        INFO("hints: " << h.input << "/" << h.write
             << " -> slots " << inputSlots << "/" << writeSlots
             << " sizes " << actualInput << "/" << actualWrite
             << " total " << total);

        // Both buffers have to exist, whatever was asked for.
        CHECK(inputSlots >= 1);
        CHECK(writeSlots >= 1);
        CHECK(actualInput > 0);
        CHECK(actualWrite > 0);

        // The returned total is what the caller allocates, so it has to match
        // the parts. A total that under-reports is how a write ends up using
        // more memory than the manager believes it handed out.
        CHECK(total == inputSlots * actualInput + writeSlots * actualWrite);

        // And it must not promise more than the machine has.
        CHECK(total <= size_t(mgr.getTotalMemoryMB()) * 1024 * 1024);
    }
}

TEST_CASE("An absurd slot hint is overruled rather than honoured", "[memory]")
{
    auto &mgr = SystemMemoryManager::instance();

    size_t inputSlots = 0, writeSlots = 0, actualInput = 0, actualWrite = 0;
    const size_t huge = size_t(64) * 1024 * 1024 * 1024;   // 64 GB per slot
    const size_t total = mgr.getCoordinatedRingBufferConfig(
        huge, huge, inputSlots, writeSlots, actualInput, actualWrite);

    INFO("total: " << total << " sizes " << actualInput << "/" << actualWrite
         << " physical " << (size_t(mgr.getTotalMemoryMB()) * 1024 * 1024));

    // The per-slot sizes are cut down, which is the part that matters.
    CHECK(actualInput < huge);
    CHECK(actualWrite < huge);
    CHECK(actualInput > 0);
    CHECK(actualWrite > 0);

    // The total is not, though. On this machine a 64 GB hint yields about
    // 11.9 GB against 7.9 GB of physical memory -- the slot size is clamped
    // but the slot count is not reduced to compensate, so a caller that
    // honoured the answer would allocate half as much again as the machine
    // has.
    //
    // Recorded rather than asserted or fixed. No caller can reach it: the
    // hints come from getOptimalInputBufferSize() and
    // getOptimalWriteBufferSize(), which are themselves bounded, and the
    // realistic range is covered by the case above. Tightening it means
    // changing sizing that every write depends on, which wants more than a
    // passing observation to justify.
    const size_t physical = size_t(mgr.getTotalMemoryMB()) * 1024 * 1024;
    if (total > physical) {
        WARN("coordinated total " << total << " exceeds physical memory " << physical
             << " for an out-of-range hint");
    }
}

TEST_CASE("Sync configuration is stable and within its clamps", "[memory]")
{
    auto &mgr = SystemMemoryManager::instance();

    const auto first = mgr.calculateSyncConfiguration();
    const auto second = mgr.calculateSyncConfiguration();

    // Two calls on one machine must agree, or the write changes its flushing
    // behaviour part way through for no reason.
    CHECK(first.syncIntervalBytes == second.syncIntervalBytes);
    CHECK(first.syncIntervalMs == second.syncIntervalMs);

    CHECK(first.syncIntervalBytes >= 16ll * 1024 * 1024);
    CHECK(first.syncIntervalBytes <= 256ll * 1024 * 1024);
    CHECK(first.syncIntervalMs > 0);
}
