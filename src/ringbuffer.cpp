/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "ringbuffer.h"
#include <QtGlobal>
#include <QString>
#include <chrono>

RingBuffer::RingBuffer(size_t numSlots, size_t slotSize, size_t alignment)
    : _numSlots(numSlots)
    , _slotSize(slotSize)
    , _alignment(alignment)
    , _committedCount(0)
    , _producerDone(false)
    , _cancelled(false)
    , _stallTimeoutExceeded(false)
    , _stallType(StallType::None)
    , _producerStalls(0)
    , _consumerStalls(0)
    , _producerWaitMs(0)
    , _consumerWaitMs(0)
    , _sessionTimer(nullptr)
{
    _slots.resize(numSlots);
    _memory.reserve(numSlots);
    _slotInUse.assign(numSlots, 0);
    _freeSlots.reserve(numSlots);
    // Pushed in reverse so the first slots handed out are 0, 1, 2, ...
    for (size_t i = numSlots; i-- > 0; ) {
        _freeSlots.push_back(i);
    }
    
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

size_t RingBuffer::slotIndex(const Slot* slot) const
{
    if (slot < _slots.data() || slot >= _slots.data() + _numSlots) {
        return _numSlots;
    }
    return static_cast<size_t>(slot - _slots.data());
}

RingBuffer::Slot* RingBuffer::acquireWriteSlot(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(_mutex);
    
    auto waitPred = [this] {
        return !_freeSlots.empty() || _cancelled || _stallTimeoutExceeded;
    };
    
    // Check if we need to wait (producer starvation)
    if (_freeSlots.empty() && !_cancelled && !_stallTimeoutExceeded) {
        _producerStalls++;
        auto waitStart = std::chrono::steady_clock::now();
        uint64_t cumulativeWaitMs = 0;
        
        // Loop with timeout, checking for overall stall timeout
        while (_freeSlots.empty() && !_cancelled && !_stallTimeoutExceeded) {
            int waitMs = timeoutMs > 0 ? timeoutMs : 100;  // Use 100ms chunks if no timeout specified
            
            if (!_writeAvailable.wait_for(lock, std::chrono::milliseconds(waitMs), waitPred)) {
                // Timeout expired - check cumulative wait time
                auto waitEnd = std::chrono::steady_clock::now();
                cumulativeWaitMs = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
                _producerWaitMs += waitMs;
                
                if (cumulativeWaitMs >= STALL_TIMEOUT_MS) {
                    // Stall timeout exceeded - this is a fatal condition
                    _stallTimeoutExceeded = true;
                    _stallType.store(StallType::ProducerStall);
                    qDebug() << "RingBuffer: Producer stall timeout exceeded after" << cumulativeWaitMs << "ms";
                    return nullptr;
                }
                
                if (timeoutMs > 0) {
                    // Caller requested specific timeout, honour it
                    return nullptr;
                }
                // Otherwise continue waiting
                continue;
            }
            break;  // Condition satisfied
        }
        
        auto waitEnd = std::chrono::steady_clock::now();
        auto waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
        _producerWaitMs += (waitDuration - cumulativeWaitMs);  // Add remaining time not counted in loop
        
        // Record significant stalls for time-series correlation
        if (waitDuration >= STALL_EVENT_THRESHOLD_MS) {
            std::lock_guard<std::mutex> eventLock(_stallEventsMutex);
            StallEvent event;
            event.timestampMs = _sessionTimer ? _sessionTimer->elapsed() : 0;
            event.durationMs = static_cast<uint32_t>(waitDuration);
            event.isProducer = true;
            _stallEvents.push(event);
        }
    }
    
    if (_cancelled || _stallTimeoutExceeded) {
        return nullptr;
    }
    
    // Take a slot that has actually been released, rather than the next index in
    // rotation -- see the _freeSlots comment in the header.
    size_t index = _freeSlots.back();
    _freeSlots.pop_back();
    _slotInUse[index] = 1;
    
    return &_slots[index];
}

void RingBuffer::commitWriteSlot(Slot* slot, size_t dataSize)
{
    if (!slot) return;
    
    slot->size = dataSize;
    
    {
        std::lock_guard<std::mutex> lock(_mutex);
        size_t index = slotIndex(slot);
        if (index == _numSlots) {
            qDebug() << "RingBuffer: commitWriteSlot() called with a foreign slot - ignoring";
            return;
        }
        // Queue the index so the consumer reads slots in commit order: the free
        // list recycles indices out of order, so it cannot be derived.
        _committedSlots.push(index);
        _committedCount++;
    }
    
    // Signal consumer that data is available
    _readAvailable.notify_one();
}

RingBuffer::Slot* RingBuffer::acquireReadSlot(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(_mutex);
    
    auto waitPred = [this] {
        return _committedCount > 0 || _producerDone || _cancelled || _stallTimeoutExceeded;
    };
    
    // Check if we need to wait (consumer starvation - waiting for producer)
    if (_committedCount == 0 && !_producerDone && !_cancelled && !_stallTimeoutExceeded) {
        _consumerStalls++;
        auto waitStart = std::chrono::steady_clock::now();
        uint64_t cumulativeWaitMs = 0;
        
        // Loop with timeout, checking for overall stall timeout
        while (_committedCount == 0 && !_producerDone && !_cancelled && !_stallTimeoutExceeded) {
            int waitMs = timeoutMs > 0 ? timeoutMs : 100;  // Use 100ms chunks if no timeout specified
            
            if (!_readAvailable.wait_for(lock, std::chrono::milliseconds(waitMs), waitPred)) {
                // Timeout expired - check cumulative wait time
                auto waitEnd = std::chrono::steady_clock::now();
                cumulativeWaitMs = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
                _consumerWaitMs += waitMs;
                
                if (cumulativeWaitMs >= STALL_TIMEOUT_MS) {
                    // Stall timeout exceeded - this is a fatal condition
                    _stallTimeoutExceeded = true;
                    _stallType.store(StallType::ConsumerStall);
                    qDebug() << "RingBuffer: Consumer stall timeout exceeded after" << cumulativeWaitMs << "ms";
                    return nullptr;
                }
                
                if (timeoutMs > 0) {
                    // Caller requested specific timeout, honour it
                    return nullptr;
                }
                // Otherwise continue waiting
                continue;
            }
            break;  // Condition satisfied
        }
        
        auto waitEnd = std::chrono::steady_clock::now();
        auto waitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(waitEnd - waitStart).count();
        _consumerWaitMs += (waitDuration - cumulativeWaitMs);  // Add remaining time not counted in loop
        
        // Record significant stalls for time-series correlation
        if (waitDuration >= STALL_EVENT_THRESHOLD_MS) {
            std::lock_guard<std::mutex> eventLock(_stallEventsMutex);
            StallEvent event;
            event.timestampMs = _sessionTimer ? _sessionTimer->elapsed() : 0;
            event.durationMs = static_cast<uint32_t>(waitDuration);
            event.isProducer = false;
            _stallEvents.push(event);
        }
    }
    
    if (_cancelled || _stallTimeoutExceeded) {
        return nullptr;
    }
    
    // Check if producer is done and no more data
    if (_committedCount == 0) {
        if (_producerDone) {
            return nullptr;  // EOF
        }
        // Spurious wakeup, try again. Release the lock first: _mutex is not
        // recursive, so recursing while still holding it would self-deadlock.
        lock.unlock();
        return acquireReadSlot(timeoutMs);
    }
    
    // Get the oldest committed slot
    size_t index = _committedSlots.front();
    _committedSlots.pop();
    _committedCount--;
    
    return &_slots[index];
}

RingBuffer::StallType RingBuffer::getStallType() const
{
    return _stallType.load();
}

void RingBuffer::releaseReadSlot(Slot* slot)
{
    if (!slot) return;
    
    slot->size = 0;  // Reset size
    
    {
        std::lock_guard<std::mutex> lock(_mutex);
        size_t index = slotIndex(slot);
        if (index == _numSlots) {
            qDebug() << "RingBuffer: releaseReadSlot() called with a foreign slot - ignoring";
            return;
        }
        if (!_slotInUse[index]) {
            // Releasing twice would put the index on the free list twice and let
            // two producers write the same buffer.
            qDebug() << "RingBuffer: slot" << index << "released while already free - ignoring";
            return;
        }
        _slotInUse[index] = 0;
        _freeSlots.push_back(index);
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
    
    _committedCount = 0;
    std::queue<size_t>().swap(_committedSlots);
    _slotInUse.assign(_numSlots, 0);
    _freeSlots.clear();
    for (size_t i = _numSlots; i-- > 0; ) {
        _freeSlots.push_back(i);
    }
    _producerDone = false;
    _cancelled = false;
    _stallTimeoutExceeded = false;
    _stallType.store(StallType::None);
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

std::vector<RingBuffer::StallEvent> RingBuffer::getPendingStallEvents()
{
    std::lock_guard<std::mutex> lock(_stallEventsMutex);
    std::vector<StallEvent> events;
    
    while (!_stallEvents.empty()) {
        events.push_back(_stallEvents.front());
        _stallEvents.pop();
    }
    
    return events;
}

