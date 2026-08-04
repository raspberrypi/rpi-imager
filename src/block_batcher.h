/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Lime Technology, Inc.
 *
 * Coalesces contiguous data blocks into larger writes.
 *
 * libarchive hands extraction one data block at a time and archive_write_data_block()
 * charges meaningful per-call overhead, so many small blocks are far slower than a few
 * large ones. That is a known libarchive limitation rather than a misuse of the API --
 * see https://github.com/libarchive/libarchive/issues/1835, still open -- and its own
 * IO notes say large blocks are almost always better.
 *
 * The penalty is worst on removable media, which is mounted with write caching disabled
 * by default (Windows "quick removal"; the kernel logs "Write cache: disabled"), so each
 * small write is flushed to the device on its own.
 *
 * This lives apart from the extraction thread so the buffering rules -- especially the
 * sparse-file behaviour, where a gap in offsets must not be silently closed up -- can be
 * tested without a network download, a ring buffer or a USB device. See
 * test/block_batcher_test.cpp.
 */

#ifndef BLOCK_BATCHER_H_
#define BLOCK_BATCHER_H_

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace rpi_imager {

class BlockBatcher {
 public:
  // Emits one write. Returns 0 on success; any other value aborts and is
  // returned to the caller unchanged (mirrors libarchive's ARCHIVE_OK == 0).
  using WriteFn = std::function<int(const void* data, std::size_t size, std::int64_t offset)>;

  BlockBatcher(std::size_t capacity, WriteFn write)
      : buffer_(capacity), write_(std::move(write)) {}

  // Queue a block. Blocks that continue the buffered run are accumulated;
  // anything else forces the pending data out first so offsets stay exact.
  int Add(const void* data, std::size_t size, std::int64_t offset) {
    if (size == 0) {
      return 0;  // nothing to write, and it must not disturb the pending run
    }

    // A jump in offsets is a hole. Emitting the pending run first keeps the
    // hole intact -- merging across it would relocate the following bytes.
    if (fill_ > 0 && offset != offset_ + static_cast<std::int64_t>(fill_)) {
      if (int r = Flush(); r != 0) return r;
    }

    // Already at least as large as the buffer: batching cannot help and would
    // only add a copy.
    if (size >= buffer_.size()) {
      if (int r = Flush(); r != 0) return r;
      return write_(data, size, offset);
    }

    if (fill_ + size > buffer_.size()) {
      if (int r = Flush(); r != 0) return r;
    }

    if (fill_ == 0) {
      offset_ = offset;
    }
    std::memcpy(buffer_.data() + fill_, data, size);
    fill_ += size;
    return 0;
  }

  // Emit whatever is pending. Safe to call when empty; must be called before
  // finishing an entry, or its tail is lost.
  int Flush() {
    if (fill_ == 0) {
      return 0;
    }
    const std::size_t size = fill_;
    const std::int64_t offset = offset_;
    // Cleared before the write so a failure cannot leave the same bytes queued
    // for a retry that would write them twice.
    fill_ = 0;
    offset_ = -1;
    return write_(buffer_.data(), size, offset);
  }

  std::size_t buffered() const { return fill_; }
  std::size_t capacity() const { return buffer_.size(); }

 private:
  std::vector<char> buffer_;
  std::size_t fill_ = 0;
  std::int64_t offset_ = -1;
  WriteFn write_;
};

}  // namespace rpi_imager

#endif  // BLOCK_BATCHER_H_
