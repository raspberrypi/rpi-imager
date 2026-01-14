/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <queue>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>

/**
 * @brief Lock-free (for single producer/consumer) ring buffer with pre-allocated slots
 * 
 * This ring buffer provides zero-copy data transfer between a producer and consumer
 * by pre-allocating fixed-size slots. The producer acquires a write slot, fills it,
 * and commits. The consumer acquires a read slot, processes data, and releases.
 * 
 * Key features:
 * - Pre-allocated memory to avoid allocation during data transfer
 * - Zero-copy: producer writes directly to buffer, consumer reads directly
 * - Blocking acquire with timeout for graceful shutdown
 * - Thread-safe for single producer / single consumer pattern
 */
class RingBuffer
{
public:
    /**
     * @brief A slot in the ring buffer
     */
    struct Slot {
        char* data;         // Pointer to pre-allocated buffer
        size_t capacity;    // Maximum capacity of this slot
        size_t size;        // Actual data size written
        
        Slot() : data(nullptr), capacity(0), size(0) {}
    };

    /**
     * @brief A stall event for performance tracking
     */
    struct StallEvent {
        qint64 timestampMs;  // When the stall occurred (from session start)
        uint32_t durationMs; // How long the stall lasted
        bool isProducer;     // true = producer stall (download waiting), false = consumer stall
    };
    
    /**
     * @brief Type of stall that caused timeout
     */
    enum class StallType {
        None,           // No stall
        ProducerStall,  // Write buffer full - consumer (disk write) is slow
        ConsumerStall   // Read buffer empty - producer (download/decompress) is slow
    };
    
    /**
     * @brief Convert StallType to human-readable string (for logging)
     */
    static const char* stallTypeToString(StallType type) {
        switch (type) {
            case StallType::None:          return "None";
            case StallType::ProducerStall: return "ProducerStall (write buffer full, disk is slow)";
            case StallType::ConsumerStall: return "ConsumerStall (read buffer empty, download is slow)";
            default:                       return "Unknown";
        }
    }

    /**
     * @brief Constructor
     * @param numSlots Number of slots in the ring buffer
     * @param slotSize Size of each slot in bytes
     * @param alignment Memory alignment for slots (default 4096 for direct I/O)
     */
    RingBuffer(size_t numSlots, size_t slotSize, size_t alignment = 4096);
    
    /**
     * @brief Destructor - frees all pre-allocated memory
     */
    ~RingBuffer();

    // Non-copyable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    /**
     * @brief Acquire a slot for writing (producer side)
     * 
     * Blocks if no slots are available until one is released.
     * 
     * @param timeoutMs Maximum time to wait in milliseconds (0 = infinite)
     * @return Pointer to slot, or nullptr if timeout/cancelled
     */
    Slot* acquireWriteSlot(int timeoutMs = 0);

    /**
     * @brief Commit a write slot after filling it with data (producer side)
     * 
     * Makes the slot available for reading by consumer.
     * 
     * @param slot The slot to commit
     * @param dataSize Actual size of data written to slot
     */
    void commitWriteSlot(Slot* slot, size_t dataSize);

    /**
     * @brief Acquire a slot for reading (consumer side)
     * 
     * Blocks if no data is available until producer commits.
     * 
     * @param timeoutMs Maximum time to wait in milliseconds (0 = infinite)
     * @return Pointer to slot with data, or nullptr if timeout/cancelled
     */
    Slot* acquireReadSlot(int timeoutMs = 0);

    /**
     * @brief Release a read slot after processing (consumer side)
     * 
     * Makes the slot available for writing again.
     * 
     * @param slot The slot to release
     */
    void releaseReadSlot(Slot* slot);

    /**
     * @brief Signal that producer is done (no more data will be written)
     */
    void producerDone();

    /**
     * @brief Check if producer has signaled completion and all data consumed
     */
    bool isComplete() const;

    /**
     * @brief Cancel all operations and wake blocked threads
     */
    void cancel();

    /**
     * @brief Check if cancelled
     */
    bool isCancelled() const { return _cancelled; }
    
    /**
     * @brief Check if stall timeout was exceeded
     * This is set when acquire operations exceed STALL_TIMEOUT_MS cumulative wait time
     */
    bool isStallTimeoutExceeded() const { return _stallTimeoutExceeded; }
    
    /**
     * @brief Get stall type (for error handling)
     * Returns the type of stall that caused the timeout
     */
    StallType getStallType() const;

    /**
     * @brief Get the capacity of each slot
     */
    size_t slotCapacity() const { return _slotSize; }

    /**
     * @brief Get number of slots
     */
    size_t numSlots() const { return _numSlots; }

    /**
     * @brief Reset the ring buffer for reuse
     */
    void reset();

    /**
     * @brief Get starvation statistics
     * @param producerStalls Number of times producer had to wait for free slots
     * @param consumerStalls Number of times consumer had to wait for data
     * @param totalProducerWaitMs Total time producer spent waiting
     * @param totalConsumerWaitMs Total time consumer spent waiting
     */
    void getStarvationStats(uint64_t& producerStalls, uint64_t& consumerStalls,
                           uint64_t& totalProducerWaitMs, uint64_t& totalConsumerWaitMs) const;

    /**
     * @brief Set the session timer for stall event timestamps
     * @param timer Pointer to QElapsedTimer started at session begin
     */
    void setSessionTimer(QElapsedTimer* timer) { _sessionTimer = timer; }

    /**
     * @brief Get and clear pending stall events (for performance logging)
     * @return Vector of stall events since last call
     */
    std::vector<StallEvent> getPendingStallEvents();

private:
    size_t _numSlots;
    size_t _slotSize;
    size_t _alignment;
    
    std::vector<Slot> _slots;
    std::vector<char*> _memory;  // Raw memory blocks for cleanup
    
    // Ring buffer indices
    std::atomic<size_t> _writeIndex;  // Next slot to write
    std::atomic<size_t> _readIndex;   // Next slot to read
    std::atomic<size_t> _committedCount;  // Number of committed (readable) slots
    std::atomic<size_t> _availableCount;  // Number of available (writable) slots
    
    // Synchronization
    std::mutex _mutex;
    std::condition_variable _writeAvailable;  // Signaled when slot available for writing
    std::condition_variable _readAvailable;   // Signaled when data available for reading
    
    // State
    std::atomic<bool> _producerDone;
    std::atomic<bool> _cancelled;
    std::atomic<bool> _stallTimeoutExceeded;
    std::atomic<StallType> _stallType;
    
    // Starvation tracking for diagnostics
    std::atomic<uint64_t> _producerStalls;      // Times producer waited for free slot
    std::atomic<uint64_t> _consumerStalls;      // Times consumer waited for data
    std::atomic<uint64_t> _producerWaitMs;      // Total producer wait time
    std::atomic<uint64_t> _consumerWaitMs;      // Total consumer wait time
    
    // Stall event queue for time-series correlation
    QElapsedTimer* _sessionTimer;               // External timer for timestamps (not owned)
    std::queue<StallEvent> _stallEvents;        // Queue of significant stall events
    std::mutex _stallEventsMutex;               // Protects _stallEvents
    
    // Stall detection constants
    // Note: These should match TimeoutDefaults in timeout_utils.h for consistency.
    // We don't include timeout_utils.h here to avoid adding dependencies to this
    // low-level data structure, but values should be kept in sync.
    static const uint32_t STALL_EVENT_THRESHOLD_MS = 50;   // = TimeoutDefaults::kRingBufferStallEventThresholdMs
    static const uint32_t STALL_TIMEOUT_MS = 30000;        // = TimeoutDefaults::kRingBufferStallTimeoutMs
};

#endif // RINGBUFFER_H

