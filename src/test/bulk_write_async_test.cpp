// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The helper's bulk writer shares a session's FileOperations between its
// worker and the dispatch thread. FileOperations is single-threaded: two
// threads reaping one io_uring completion stalled the ring for minutes.

#include <catch2/catch_test_macros.hpp>

#include "bulk_write_async.h"
#include "wire_outbound.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

namespace wire = rpi_imager::privileged::wire;
namespace proto = rpi_imager::privileged::proto;
using rpi_imager::FileError;
using rpi_imager::FileOperations;
using rpi_imager::WriteErrorClass;

namespace {

class DiscardStream final : public wire::DuplexStream {
public:
    bool write(const char*, std::size_t, int) override { return true; }
    wire::StreamChunk read(char*, std::size_t) override { return {}; }
    void cancel() override {}
    int lastError() const override { return 0; }
};

// Completes writes only when polled, and lingers after each callback as a
// reap of several completions would. Counts any moment two threads are in.
class SharedDevice final : public FileOperations {
public:
    std::atomic<int> overlaps{0};

    FileError AsyncWriteSequential(const std::uint8_t*, std::size_t size,
                                   AsyncWriteCallback callback) override {
        Inside in(*this);
        std::lock_guard<std::mutex> lk(m_);
        pending_.push_back({size, std::move(callback)});
        cursor_ += size;
        return FileError::kSuccess;
    }
    void PollAsyncCompletions() override {
        Inside in(*this);
        completeOne();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    FileError WaitForPendingWrites() override {
        Inside in(*this);
        while (completeOne()) {
        }
        return FileError::kSuccess;
    }
    FileError ForceSync() override { Inside in(*this); return FileError::kSuccess; }
    FileError Seek(std::uint64_t position) override {
        Inside in(*this);
        cursor_ = position;
        return FileError::kSuccess;
    }
    std::uint64_t Tell() const override { return cursor_; }
    int GetPendingWriteCount() const override {
        std::lock_guard<std::mutex> lk(m_);
        return static_cast<int>(pending_.size());
    }

    FileError OpenDevice(const std::string&) override { return FileError::kSuccess; }
    FileError CreateTestFile(const std::string&, std::uint64_t) override { return FileError::kSuccess; }
    FileError WriteAtOffset(std::uint64_t, const std::uint8_t*, std::size_t) override { return FileError::kSuccess; }
    FileError GetSize(std::uint64_t& size) override { size = 1u << 20; return FileError::kSuccess; }
    FileError Close() override { return FileError::kSuccess; }
    bool IsOpen() const override { return true; }
    FileError WriteSequential(const std::uint8_t*, std::size_t) override { return FileError::kSuccess; }
    FileError ReadSequential(std::uint8_t*, std::size_t, std::size_t& n) override { n = 0; return FileError::kSuccess; }
    bool SetAsyncQueueDepth(int) override { return true; }
    int GetAsyncQueueDepth() const override { return 8; }
    bool IsAsyncIOSupported() const override { return true; }
    void CancelAsyncIO() override {}
    FileError AttemptSyncFallback() override { return FileError::kSuccess; }
    std::vector<PendingWriteInfo> GetPendingWritesSorted() const override { return {}; }
    void ReduceQueueDepthForRecovery(int) override {}
    bool DrainAndSwitchToSync(int) override { return true; }
    FileError Flush() override { return FileError::kSuccess; }
    void PrepareForSequentialRead(std::uint64_t, std::uint64_t) override {}
    int GetHandle() const override { return -1; }
    int GetLastErrorCode() const override { return 0; }
    WriteErrorClass ClassifyLastWriteError() const override { return WriteErrorClass{}; }
    bool IsDirectIOEnabled() const override { return false; }
    FileError SetDirectIOEnabled(bool) override { return FileError::kSuccess; }
    DirectIOInfo GetDirectIOInfo() const override { return {}; }

private:
    struct Inside {
        explicit Inside(SharedDevice& d) : d_(d) {
            if (d_.inside_.fetch_add(1) != 0) {
                d_.overlaps.fetch_add(1);
            }
        }
        ~Inside() { d_.inside_.fetch_sub(1); }
        SharedDevice& d_;
    };

    bool completeOne() {
        Pending p;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (pending_.empty()) {
                return false;
            }
            p = std::move(pending_.front());
            pending_.pop_front();
        }
        if (p.callback) {
            p.callback(FileError::kSuccess, p.size);
        }
        return true;
    }

    struct Pending {
        std::size_t size = 0;
        AsyncWriteCallback callback;
    };
    mutable std::mutex m_;
    std::deque<Pending> pending_;
    std::uint64_t cursor_ = 0;
    std::atomic<int> inside_{0};
};

} // namespace

TEST_CASE("A sync waits for the worker to leave the device", "[bulk-writer]")
{
    SharedDevice device;
    DiscardStream stream;
    rpi_imager::writer::WireOutbound outbound(stream);
    rpi_imager::writer::SessionBulkWriter writer;
    writer.start(&device, &outbound);

    std::atomic<std::uint64_t> written{0};
    rpi_imager::writer::SessionTelemetry telemetry;
    std::vector<std::uint8_t> data(4096, 0x5A);

    for (int round = 0; round < 20; ++round) {
        proto::WriteRequest req;
        req.set_offset(static_cast<std::uint64_t>(round) * data.size());
        req.set_length(data.size());
        proto::ErrorInfo err;
        REQUIRE(writer.submit(static_cast<std::uint64_t>(round + 1), req, data.data(),
                              1ull << 40, &written, &telemetry, err));
        // The worker's reap fires the callback that lets this return, then
        // stays in the device for a while yet.
        const auto held = writer.drainBeforeSync(&device);
        REQUIRE(held.owns_lock());
        CHECK(device.ForceSync() == FileError::kSuccess);
    }

    writer.shutdown();
    CHECK(device.overlaps.load() == 0);
    CHECK(written.load() == 20 * data.size());
}

TEST_CASE("A device call from the dispatch thread waits for a reap", "[bulk-writer]")
{
    SharedDevice device;
    DiscardStream stream;
    rpi_imager::writer::WireOutbound outbound(stream);
    rpi_imager::writer::SessionBulkWriter writer;
    writer.start(&device, &outbound);

    std::atomic<std::uint64_t> written{0};
    rpi_imager::writer::SessionTelemetry telemetry;
    std::vector<std::uint8_t> data(4096, 0x3C);

    for (int round = 0; round < 20; ++round) {
        proto::WriteRequest req;
        req.set_offset(static_cast<std::uint64_t>(round) * data.size());
        req.set_length(data.size());
        proto::ErrorInfo err;
        REQUIRE(writer.submit(static_cast<std::uint64_t>(round + 1), req, data.data(),
                              1ull << 40, &written, &telemetry, err));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        // What readChunk does: seek and read while writes may be in flight.
        const auto held = writer.lockDevice();
        CHECK(device.Seek(0) == FileError::kSuccess);
    }

    (void)writer.drainBeforeSync(&device);
    writer.shutdown();
    CHECK(device.overlaps.load() == 0);
}
