/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef ALIGNED_BUFFER_H
#define ALIGNED_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace rpi_imager {

/**
 * @brief Get the system's required alignment for direct I/O operations
 * 
 * O_DIRECT on Linux requires buffers to be aligned to the filesystem's
 * logical block size. The system page size is always >= the block size,
 * so we use that as a safe alignment value.
 * 
 * @return The alignment in bytes (typically 4096, but can be larger on some systems)
 */
inline std::size_t GetDirectIOAlignment() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    long page_size = sysconf(_SC_PAGESIZE);
    // Fallback to 4096 if sysconf fails (returns -1)
    return (page_size > 0) ? static_cast<std::size_t>(page_size) : 4096;
#endif
}

/**
 * @brief RAII wrapper for aligned memory allocation
 * 
 * This class provides a safe, cross-platform way to allocate memory with
 * specific alignment requirements. It's primarily used for O_DIRECT I/O
 * on Linux, which requires buffers to be aligned to the filesystem's
 * logical block size.
 * 
 * Features:
 * - Automatic cleanup via RAII
 * - Cross-platform support (Windows, Linux, macOS)
 * - Queries system for optimal alignment
 * - Zero-initializes the buffer
 * 
 * Usage:
 * @code
 *   AlignedBuffer buffer(4096);  // Allocate 4KB aligned buffer
 *   if (buffer) {
 *       memcpy(buffer.data(), source, size);
 *       write(fd, buffer.data(), buffer.size());
 *   }
 * @endcode
 */
class AlignedBuffer {
public:
    /**
     * @brief Construct an aligned buffer of the specified size
     * 
     * The buffer is automatically aligned to the system's direct I/O alignment
     * requirement (typically 4096 bytes) and zero-initialized.
     * 
     * @param size The minimum size of the buffer in bytes
     */
    explicit AlignedBuffer(std::size_t size) : size_(size), data_(nullptr), alignment_(0) {
        if (size == 0) return;
        
        // Query the system for the required alignment
        alignment_ = GetDirectIOAlignment();
        
        // Round up size to alignment boundary for O_DIRECT compatibility
        std::size_t aligned_size = ((size + alignment_ - 1) / alignment_) * alignment_;
        
#ifdef _WIN32
        data_ = static_cast<std::uint8_t*>(_aligned_malloc(aligned_size, alignment_));
#else
        // Use posix_memalign for POSIX systems (more portable than aligned_alloc)
        void* ptr = nullptr;
        if (posix_memalign(&ptr, alignment_, aligned_size) == 0) {
            data_ = static_cast<std::uint8_t*>(ptr);
        }
#endif
        
        if (data_) {
            std::memset(data_, 0, aligned_size);
        }
    }
    
    /**
     * @brief Destructor - frees the aligned memory
     */
    ~AlignedBuffer() {
        free();
    }
    
    // Non-copyable
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    
    // Movable
    AlignedBuffer(AlignedBuffer&& other) noexcept 
        : size_(other.size_), data_(other.data_), alignment_(other.alignment_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.alignment_ = 0;
    }
    
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            free();
            data_ = other.data_;
            size_ = other.size_;
            alignment_ = other.alignment_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.alignment_ = 0;
        }
        return *this;
    }
    
    /**
     * @brief Get a pointer to the buffer data
     */
    std::uint8_t* data() { return data_; }
    const std::uint8_t* data() const { return data_; }
    
    /**
     * @brief Get the requested size of the buffer
     * 
     * Note: The actual allocated size may be larger due to alignment rounding
     */
    std::size_t size() const { return size_; }
    
    /**
     * @brief Get the alignment used for this buffer
     */
    std::size_t alignment() const { return alignment_; }
    
    /**
     * @brief Check if the buffer was successfully allocated
     */
    bool valid() const { return data_ != nullptr; }
    
    /**
     * @brief Explicit bool conversion for validity checking
     */
    explicit operator bool() const { return valid(); }
    
    /**
     * @brief Get a typed pointer to the buffer data
     * 
     * @tparam T The type to cast to
     * @param offset Byte offset into the buffer (default 0)
     * @return Pointer to the data cast to type T*
     */
    template<typename T>
    T* as(std::size_t offset = 0) {
        return reinterpret_cast<T*>(data_ + offset);
    }
    
    template<typename T>
    const T* as(std::size_t offset = 0) const {
        return reinterpret_cast<const T*>(data_ + offset);
    }

private:
    void free() {
        if (data_) {
#ifdef _WIN32
            _aligned_free(data_);
#else
            std::free(data_);
#endif
            data_ = nullptr;
        }
    }

    std::size_t size_;
    std::uint8_t* data_;
    std::size_t alignment_;
};

}  // namespace rpi_imager

#endif  // ALIGNED_BUFFER_H

