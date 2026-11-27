/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef WRITE_BUFFER_PROVIDER_H
#define WRITE_BUFFER_PROVIDER_H

#include <cstddef>
#include <vector>
#include <QtGlobal>

/**
 * @brief Supplies the backing memory for a RingBuffer's slots.
 *
 * RingBuffer owns the queueing and back-pressure logic but delegates *where
 * the slot bytes live* to a provider. This keeps RingBuffer platform-agnostic
 * while letting a platform backend decide the memory's provenance.
 *
 * The default (HeapWriteBufferProvider) uses aligned heap allocation, which is
 * behaviourally identical to RingBuffer's historical inline allocation. A
 * backend that pre-maps device-write memory (e.g. the macOS helper's
 * shared-memory bulk-write ring) can instead return a provider whose slots
 * live in that memory, so the producer fills device-write memory in place and
 * the write path needs no intermediate copy (see isZeroCopy()).
 *
 * Lifetime contract: RingBuffer drains/cancels so no slot is in use before it
 * calls releaseSlots(); a provider must keep handed-out pointers valid until
 * then. A zero-copy provider whose memory is owned elsewhere (e.g. an mmap of
 * a helper-owned region) must outlive any in-flight device writes referencing
 * it — the consumer is responsible for draining those before release.
 */
class WriteBufferProvider {
public:
    virtual ~WriteBufferProvider() = default;

    /**
     * @brief Allocate slot memory.
     * @param count     Number of slots to allocate.
     * @param slotSize  Size of each slot in bytes.
     * @param alignment Required base alignment of each slot.
     * @param outSlots  Filled with exactly @p count base pointers on success.
     * @return true on success; on failure nothing remains allocated.
     */
    virtual bool allocateSlots(std::size_t count, std::size_t slotSize,
                               std::size_t alignment,
                               std::vector<void*>& outSlots) = 0;

    /**
     * @brief Release everything allocated by the last allocateSlots().
     * Previously handed-out pointers become invalid.
     */
    virtual void releaseSlots() = 0;

    /**
     * @brief Whether slot memory is written to the device without a copy.
     *
     * Returns true if the provided memory is consumed directly by the write
     * path (e.g. shared memory the privileged backend pwrites from). Consumers
     * can use this to select a zero-copy submit path; false means the slot is
     * ordinary memory that the write path copies/streams as usual.
     */
    virtual bool isZeroCopy() const { return false; }
};

/**
 * @brief Default provider: one page-aligned heap block per slot.
 *
 * Matches RingBuffer's historical allocation behaviour exactly.
 */
class HeapWriteBufferProvider : public WriteBufferProvider {
public:
    ~HeapWriteBufferProvider() override { releaseSlots(); }

    bool allocateSlots(std::size_t count, std::size_t slotSize,
                       std::size_t alignment,
                       std::vector<void*>& outSlots) override {
        releaseSlots();
        _blocks.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            void* mem = qMallocAligned(slotSize, alignment);
            if (!mem) {
                releaseSlots();
                return false;
            }
            _blocks.push_back(mem);
        }
        outSlots = _blocks;
        return true;
    }

    void releaseSlots() override {
        for (void* p : _blocks) {
            qFreeAligned(p);
        }
        _blocks.clear();
    }

private:
    std::vector<void*> _blocks;
};

#endif // WRITE_BUFFER_PROVIDER_H
