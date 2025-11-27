/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "ringbuffer.h"
#include <QtGlobal>
#include <chrono>

RingBuffer::RingBuffer(size_t numSlots, size_t slotSize, size_t alignment)
    : _numSlots(numSlots)
    , _slotSize(slotSize)
    , _alignment(alignment)
    , _writeIndex(0)
    , _readIndex(0)
    , _committedCount(0)
    , _availableCount(numSlots)
    , _producerDone(false)
    , _cancelled(false)
    , _producerStalls(0)
    , _consumerStalls(0)
    , _producerWaitMs(0)
    , _consumerWaitMs(0)
{
    _slots.resize(numSlots);
    _memory.reserve(numSlots);
    
    // Pre-allocate aligned memory for each slot
    for (size_t i = 0; i < numSlots; ++i) {
        char* mem = static_cast<char*>(qMallocAligned(slotSize, alignment));
        if (!mem) {
            qDebug() << "RingBuffer: Failed to allocate slot" << i;
            // Clean up already allocated
            for (char* ptr : _memory) {
                qFreeAligned(ptr);
            }
            _memory.clear();
            throw std::bad_alloc();
        }
        _memory.push_back(mem);
        _slots[i].data = mem;
        _slots[i].capacity = slotSize;
        _slots[i].size = 0;
    }
    
    qDebug() << "RingBuffer: Allocated" << numSlots << "slots of" 
             << slotSize / 1024 << "KB each (" << (numSlots * slotSize) / (1024 * 1024) << "MB total)";
}

RingBuffer::~RingBuffer()
{
    cancel();  // Wake any blocked threads
    
    // Log final stats if there was any starvation
    if (_producerStalls > 0 || _consumerStalls > 0) {
        qDebug() << "RingBuffer final stats:"
                 << "producer stalls:" << _producerStalls.load()
                 << "(" << _producerWaitMs.load() << "ms),"
                 << "consumer stalls:" << _consumerStalls.load()
                 << "(" << _consumerWaitMs.load() << "ms)";
        
        // Provide guidance on starvation issues
        if (_producerWaitMs > 1000) {
            qDebug() << "RingBuffer: High producer wait time suggests disk/decompression is slower than download.";
        }
        if (_consumerWaitMs > 5000) {
            qDebug() << "RingBuffer: High consumer wait time suggests download is slower than processing.";
        }
    }
    
    // Free all allocated memory
    for (char* ptr : _memory) {
        qFreeAligned(ptr);
    }
    _memory.clear();
}

RingBuffer::Slot* RingBuffer::acquireWriteSlot(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(_mutex);
    
    auto waitPred = [this] {
        return _availableCount > 0 || _cancelled;
    };
    
    // Check if we need to wait (producer starvation)
    if (_availableCount == 0 && !_cancelled) {
        _producerStalls++;
        auto waitStart = std::chrono::steady_clock::now();
        
        if (timeoutMs > 0) {
            if (!_writeAvailable.wait_for(lock, std::chrono::milliseconds(timeoutMs), waitPred)) {
                auto waitEnd = std::chrono::steady_clock::now();
                auto waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
                _producerWaitMs += waitDuration;
                
                if (waitDuration > 50) {  // Only log significant waits
                    qDebug() << "RingBuffer: Producer stall - waited" << waitDuration 
                             << "ms for free slot (timeout). Total stalls:" << _producerStalls.load();
                }
                return nullptr;  // Timeout
            }
        } else {
            _writeAvailable.wait(lock, waitPred);
        }
        
        auto waitEnd = std::chrono::steady_clock::now();
        auto waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
        _producerWaitMs += waitDuration;
        
        if (waitDuration > 100) {  // Log waits > 100ms
            qDebug() << "RingBuffer: Producer stall - waited" << waitDuration 
                     << "ms for free slot. Consumer may be slow. Total stalls:" << _producerStalls.load();
        }
    }
    
    if (_cancelled) {
        return nullptr;
    }
    
    // Get the next write slot
    size_t index = _writeIndex % _numSlots;
    Slot* slot = &_slots[index];
    
    // Advance write index and decrement available count
    _writeIndex++;
    _availableCount--;
    
    return slot;
}

void RingBuffer::commitWriteSlot(Slot* slot, size_t dataSize)
{
    if (!slot) return;
    
    slot->size = dataSize;
    
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _committedCount++;
    }
    
    // Signal consumer that data is available
    _readAvailable.notify_one();
}

RingBuffer::Slot* RingBuffer::acquireReadSlot(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(_mutex);
    
    auto waitPred = [this] {
        return _committedCount > 0 || _producerDone || _cancelled;
    };
    
    // Check if we need to wait (consumer starvation - waiting for producer)
    if (_committedCount == 0 && !_producerDone && !_cancelled) {
        _consumerStalls++;
        auto waitStart = std::chrono::steady_clock::now();
        
        if (timeoutMs > 0) {
            if (!_readAvailable.wait_for(lock, std::chrono::milliseconds(timeoutMs), waitPred)) {
                auto waitEnd = std::chrono::steady_clock::now();
                auto waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
                _consumerWaitMs += waitDuration;
                
                // Don't log short timeouts - they're expected during normal polling
                return nullptr;  // Timeout
            }
        } else {
            _readAvailable.wait(lock, waitPred);
        }
        
        auto waitEnd = std::chrono::steady_clock::now();
        auto waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
        _consumerWaitMs += waitDuration;
        
        if (waitDuration > 500) {  // Log waits > 500ms (significant network/download delay)
            qDebug() << "RingBuffer: Consumer stall - waited" << waitDuration 
                     << "ms for data. Download may be slow. Total stalls:" << _consumerStalls.load();
        }
    }
    
    if (_cancelled) {
        return nullptr;
    }
    
    // Check if producer is done and no more data
    if (_committedCount == 0) {
        if (_producerDone) {
            return nullptr;  // EOF
        }
        // Spurious wakeup, try again
        return acquireReadSlot(timeoutMs);
    }
    
    // Get the next read slot
    size_t index = _readIndex % _numSlots;
    Slot* slot = &_slots[index];
    
    // Advance read index and decrement committed count
    _readIndex++;
    _committedCount--;
    
    return slot;
}

void RingBuffer::releaseReadSlot(Slot* slot)
{
    if (!slot) return;
    
    slot->size = 0;  // Reset size
    
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _availableCount++;
    }
    
    // Signal producer that slot is available
    _writeAvailable.notify_one();
}

void RingBuffer::producerDone()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _producerDone = true;
    }
    
    // Wake consumer in case it's waiting
    _readAvailable.notify_all();
}

bool RingBuffer::isComplete() const
{
    return _producerDone && _committedCount == 0;
}

void RingBuffer::cancel()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _cancelled = true;
    }
    
    // Wake all waiting threads
    _writeAvailable.notify_all();
    _readAvailable.notify_all();
}

void RingBuffer::reset()
{
    // Log stats before reset if there was any activity
    if (_producerStalls > 0 || _consumerStalls > 0) {
        qDebug() << "RingBuffer stats before reset:"
                 << "producer stalls:" << _producerStalls.load()
                 << "(" << _producerWaitMs.load() << "ms total),"
                 << "consumer stalls:" << _consumerStalls.load()
                 << "(" << _consumerWaitMs.load() << "ms total)";
    }
    
    std::lock_guard<std::mutex> lock(_mutex);
    
    _writeIndex = 0;
    _readIndex = 0;
    _committedCount = 0;
    _availableCount = _numSlots;
    _producerDone = false;
    _cancelled = false;
    _producerStalls = 0;
    _consumerStalls = 0;
    _producerWaitMs = 0;
    _consumerWaitMs = 0;
    
    // Reset all slot sizes
    for (auto& slot : _slots) {
        slot.size = 0;
    }
}

void RingBuffer::getStarvationStats(uint64_t& producerStalls, uint64_t& consumerStalls,
                                    uint64_t& totalProducerWaitMs, uint64_t& totalConsumerWaitMs) const
{
    producerStalls = _producerStalls.load();
    consumerStalls = _consumerStalls.load();
    totalProducerWaitMs = _producerWaitMs.load();
    totalConsumerWaitMs = _consumerWaitMs.load();
}

