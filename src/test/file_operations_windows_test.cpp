/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The Windows file-operations backend: the code that actually writes a card.
 *
 * file_operations_test.cpp covers the POSIX one and cannot be built here -- it
 * is written against O_DIRECT, descriptor-level I/O and a loop device. This is
 * the Windows equivalent of the parts that are about behaviour rather than
 * about POSIX: the open and close lifecycle, sequential and positioned I/O,
 * the asynchronous queue and its fallback to synchronous writing, and what the
 * backend says when it cannot do what it was asked.
 *
 * Most of it runs against a scratch file, which is what the writer is pointed
 * at for a write to a file and is enough to reach the great majority of the
 * branches. The cases needing a real physical drive take a VHD, and skip where
 * the process cannot attach one.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "file_operations.h"
#include "platform_permissions.h"
#include "platform_privilege.h"
#include "timeout_utils.h"
#include "vhd_device.h"
// Reached directly for the fault-injection seam, which is on the backend
// rather than on the interface every platform shares.
#include "windows/file_operations_windows.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstring>
#include <string>
#include <QUuid>

#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

namespace {

using rpi_imager::FileError;
using rpi_imager::FileOperations;

// A scratch file of a known size, and the backend opened on it.
class ScratchDevice
{
public:
    explicit ScratchDevice(std::uint64_t bytes = 1u * 1024 * 1024)
    {
        REQUIRE(_dir.isValid());
        _path = QDir(_dir.path()).filePath(QStringLiteral("scratch.img")).toStdString();
        auto maker = FileOperations::Create();
        REQUIRE(maker->CreateTestFile(_path, bytes) == FileError::kSuccess);
        _ops = FileOperations::Create();
        REQUIRE(_ops->OpenDevice(_path) == FileError::kSuccess);
    }

    FileOperations *operator->() const { return _ops.get(); }
    const std::string &path() const { return _path; }

private:
    QTemporaryDir _dir;
    std::string _path;
    std::unique_ptr<FileOperations> _ops;
};

std::vector<std::uint8_t> pattern(std::size_t n, std::uint8_t seed = 0)
{
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<std::uint8_t>((i * 31 + seed) & 0xFF);
    return v;
}

} // namespace

// ============================================================================
// Opening and closing
// ============================================================================

TEST_CASE("A scratch device opens, reports its size and closes", "[fileops-win]")
{
    ScratchDevice dev(2u * 1024 * 1024);
    CHECK(dev->IsOpen());

    std::uint64_t size = 0;
    REQUIRE(dev->GetSize(size) == FileError::kSuccess);
    CHECK(size == 2u * 1024 * 1024);

    CHECK(dev->Close() == FileError::kSuccess);
    CHECK_FALSE(dev->IsOpen());
}

TEST_CASE("Closing twice is not an error", "[fileops-win]")
{
    // The write path closes on its own success and again from the destructor.
    ScratchDevice dev;
    CHECK(dev->Close() == FileError::kSuccess);
    CHECK(dev->Close() == FileError::kSuccess);
}

TEST_CASE("A path that is not there is refused, and says why", "[fileops-win]")
{
    auto ops = FileOperations::Create();
    const auto err = ops->OpenDevice("C:/nowhere-at-all/rpi-imager/missing.img");
    CHECK(err != FileError::kSuccess);
    CHECK_FALSE(ops->IsOpen());
    // The Windows error code is carried out rather than collapsed, so a caller
    // can tell "not there" from "not allowed".
    CHECK(ops->GetLastErrorCode() != 0);
}

TEST_CASE("A file the user cannot open is refused", "[fileops-win]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("denied.img"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, '\0'));
    }

    rpi_test::DeniedAccess denied(path, rpi_test::DeniedAccess::Read);
    REQUIRE_DENIED(denied);

    auto ops = FileOperations::Create();
    CHECK(ops->OpenDevice(path.toStdString()) != FileError::kSuccess);
    CHECK_FALSE(ops->IsOpen());
}

// ============================================================================
// Reading and writing
// ============================================================================

TEST_CASE("Bytes written sequentially read back unchanged", "[fileops-win]")
{
    ScratchDevice dev;
    const auto data = pattern(64 * 1024);

    REQUIRE(dev->WriteSequential(data.data(), data.size()) == FileError::kSuccess);
    REQUIRE(dev->Flush() == FileError::kSuccess);

    REQUIRE(dev->Seek(0) == FileError::kSuccess);
    CHECK(dev->Tell() == 0);

    std::vector<std::uint8_t> got(data.size());
    std::size_t read = 0;
    REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
    CHECK(read == data.size());
    CHECK(got == data);
}

TEST_CASE("A positioned write lands where it was addressed", "[fileops-win]")
{
    ScratchDevice dev;
    const auto data = pattern(8192, 7);
    constexpr std::uint64_t kOffset = 128 * 1024;

    REQUIRE(dev->WriteAtOffset(kOffset, data.data(), data.size()) == FileError::kSuccess);
    REQUIRE(dev->Flush() == FileError::kSuccess);

    REQUIRE(dev->Seek(kOffset) == FileError::kSuccess);
    CHECK(dev->Tell() == kOffset);

    std::vector<std::uint8_t> got(data.size());
    std::size_t read = 0;
    REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
    CHECK(read == data.size());
    CHECK(got == data);
}

TEST_CASE("Reading past the end returns nothing rather than rubbish",
          "[fileops-win]")
{
    ScratchDevice dev(64 * 1024);
    REQUIRE(dev->Seek(64 * 1024) == FileError::kSuccess);

    std::vector<std::uint8_t> got(512, 0xAB);
    std::size_t read = 1;
    const auto err = dev->ReadSequential(got.data(), got.size(), read);
    // Either a clean end of file or a refusal. What must not happen is a
    // success reporting bytes nobody wrote.
    if (err == FileError::kSuccess)
        CHECK(read == 0);
}

TEST_CASE("A sync on an open device succeeds", "[fileops-win]")
{
    ScratchDevice dev;
    const auto data = pattern(4096);
    REQUIRE(dev->WriteSequential(data.data(), data.size()) == FileError::kSuccess);
    CHECK(dev->ForceSync() == FileError::kSuccess);
}

// ============================================================================
// The asynchronous queue
// ============================================================================

TEST_CASE("The async queue depth is settable and reported back", "[fileops-win]")
{
    ScratchDevice dev;
    CHECK(dev->GetAsyncQueueDepth() >= 1);

    if (dev->SetAsyncQueueDepth(8))
        CHECK(dev->GetAsyncQueueDepth() == 8);

    // Nothing outstanding on a queue nothing has been put on.
    CHECK(dev->GetPendingWriteCount() == 0);
}

TEST_CASE("Asynchronous writes complete and read back unchanged", "[fileops-win]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");

    dev->SetAsyncQueueDepth(4);
    const auto data = pattern(32 * 1024, 3);

    // The callback records what the queue reported, so the case checks the
    // completion rather than only that the write was accepted.
    FileError completed = FileError::kWriteError;  // anything but kSuccess
    std::uint64_t completedBytes = 0;
    REQUIRE(dev->AsyncWriteSequential(
                data.data(), data.size(),
                [&](FileError e, std::uint64_t n) { completed = e; completedBytes = n; }) ==
            FileError::kSuccess);
    REQUIRE(dev->WaitForPendingWrites() == FileError::kSuccess);
    CHECK(dev->GetPendingWriteCount() == 0);
    CHECK(completed == FileError::kSuccess);
    CHECK(completedBytes == data.size());

    REQUIRE(dev->Seek(0) == FileError::kSuccess);
    std::vector<std::uint8_t> got(data.size());
    std::size_t read = 0;
    REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
    CHECK(read == data.size());
    CHECK(got == data);
}

// A queue with enough on it that the wait has something to do.
//
// One small write completes before anything can look, so the drain loop --
// the completion port wait, the context lookup, the callbacks -- was never
// entered by any case. Depth and count are chosen so writes are still in
// flight when the wait begins.
namespace {
struct QueuedWrites {
    std::vector<std::vector<std::uint8_t>> buffers;
    std::atomic<int> completed{0};
    std::atomic<int> failed{0};
};

// Returns false when the backend has no async path, so a case can skip.
bool fillTheQueue(ScratchDevice &dev, QueuedWrites &out, int writes, std::size_t each)
{
    if (!dev->IsAsyncIOSupported())
        return false;
    dev->SetAsyncQueueDepth(32);
    out.buffers.reserve(writes);
    for (int i = 0; i < writes; ++i) {
        out.buffers.push_back(pattern(each, static_cast<std::uint8_t>(i)));
        const auto &buf = out.buffers.back();
        const FileError queued = dev->AsyncWriteSequential(
            buf.data(), buf.size(),
            [&out](FileError e, std::uint64_t) {
                if (e == FileError::kSuccess)
                    out.completed.fetch_add(1);
                else
                    out.failed.fetch_add(1);
            });
        if (queued != FileError::kSuccess)
            return false;
    }
    return true;
}
} // namespace

TEST_CASE("Waiting drains a queue that is still in flight", "[fileops-win]")
{
    ScratchDevice dev(48u * 1024 * 1024);
    QueuedWrites queued;
    if (!fillTheQueue(dev, queued, 96, 256 * 1024))
        SKIP("this backend has no asynchronous path to exercise");

    // Without something still on the wire the wait returns down its empty
    // path and the case proves nothing, so this is the premise rather than a
    // nicety.
    const int inFlight = dev->GetPendingWriteCount();
    INFO("writes still in flight when the wait began: " << inFlight);
    CHECK(inFlight > 0);

    REQUIRE(dev->WaitForPendingWrites() == FileError::kSuccess);

    // Every write is accounted for, and none is still owed a callback: a
    // context left in the table is a buffer the caller has been told nothing
    // about and cannot free.
    CHECK(dev->GetPendingWriteCount() == 0);
    CHECK(queued.completed.load() + queued.failed.load() == 96);
    CHECK(queued.failed.load() == 0);
}

TEST_CASE("Cancelling a queue in flight drains it rather than leaking",
          "[fileops-win]")
{
    // What pressing Cancel mid-write reaches. The writes already on the wire
    // cannot be recalled, so they have to be waited out and their callbacks
    // run -- returning early would free buffers the device is still reading.
    ScratchDevice dev(48u * 1024 * 1024);
    QueuedWrites queued;
    if (!fillTheQueue(dev, queued, 96, 256 * 1024))
        SKIP("this backend has no asynchronous path to exercise");

    dev->CancelAsyncIO();
    const FileError waited = dev->WaitForPendingWrites();
    CHECK((waited == FileError::kSuccess || waited == FileError::kCancelled));

    CHECK(dev->GetPendingWriteCount() == 0);
    CHECK(queued.completed.load() + queued.failed.load() == 96);
}

TEST_CASE("The pending list is ordered by where each write goes",
          "[fileops-win]")
{
    // The order matters to the sync fallback, which continues from the lowest
    // offset that has not landed. Asked of an empty queue elsewhere; asked
    // here of one with writes on it.
    ScratchDevice dev(16u * 1024 * 1024);
    QueuedWrites queued;
    if (!fillTheQueue(dev, queued, 24, 128 * 1024))
        SKIP("this backend has no asynchronous path to exercise");

    const auto sorted = dev->GetPendingWritesSorted();
    for (std::size_t i = 1; i < sorted.size(); ++i)
        CHECK(sorted[i - 1].offset <= sorted[i].offset);

    REQUIRE(dev->WaitForPendingWrites() == FileError::kSuccess);
    CHECK(dev->GetPendingWritesSorted().empty());
}

TEST_CASE("Draining a queue that is not empty switches to synchronous mode",
          "[fileops-win]")
{
    // Asked of an empty queue elsewhere, which returns before the drain loop.
    // With writes on it the loop polls completions until the count reaches
    // nought, which is what a stalling card puts it through.
    ScratchDevice dev(48u * 1024 * 1024);
    QueuedWrites queued;
    if (!fillTheQueue(dev, queued, 96, 256 * 1024))
        SKIP("this backend has no asynchronous path to exercise");

    REQUIRE(dev->DrainAndSwitchToSync(30));
    CHECK(dev->IsInSyncFallbackMode());
    CHECK(dev->GetPendingWriteCount() == 0);
    CHECK(queued.completed.load() + queued.failed.load() == 96);
}

TEST_CASE("Waiting with nothing outstanding returns at once", "[fileops-win]")
{
    ScratchDevice dev;
    CHECK(dev->WaitForPendingWrites() == FileError::kSuccess);
    CHECK(dev->GetPendingWritesSorted().empty());
}

TEST_CASE("Cancelling with nothing outstanding is safe", "[fileops-win]")
{
    // Called from the destructor and from cancelDownload(), either of which
    // can arrive before a single write has been queued.
    ScratchDevice dev;
    CHECK_NOTHROW(dev->CancelAsyncIO());
    CHECK_NOTHROW(dev->CancelAsyncIO());
}

TEST_CASE("The backend can be pushed into synchronous mode and still writes",
          "[fileops-win]")
{
    ScratchDevice dev;
    CHECK_FALSE(dev->IsInSyncFallbackMode());

    if (!dev->DrainAndSwitchToSync(1))
        SKIP("this backend does not offer a synchronous fallback");

    CHECK(dev->IsInSyncFallbackMode());

    const auto data = pattern(8192, 11);
    REQUIRE(dev->WriteSequential(data.data(), data.size()) == FileError::kSuccess);
    REQUIRE(dev->Seek(0) == FileError::kSuccess);
    std::vector<std::uint8_t> got(data.size());
    std::size_t read = 0;
    REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
    CHECK(got == data);
}

TEST_CASE("Reducing the queue depth for recovery is accepted", "[fileops-win]")
{
    ScratchDevice dev;
    dev->SetAsyncQueueDepth(16);
    CHECK_NOTHROW(dev->ReduceQueueDepthForRecovery(2));
    CHECK(dev->GetAsyncQueueDepth() <= 16);
}

TEST_CASE("Async statistics start empty and count what was written",
          "[fileops-win]")
{
    ScratchDevice dev;
    std::uint32_t wall = 1, count = 1, minUs = 1, maxUs = 1, avgUs = 1;
    dev->ResetAsyncIOStats();
    dev->GetAsyncIOStats(wall, count, minUs, maxUs, avgUs);
    CHECK(count == 0);

    const auto data = pattern(4096);
    if (dev->IsAsyncIOSupported() &&
        dev->AsyncWriteSequential(data.data(), data.size()) == FileError::kSuccess) {
        REQUIRE(dev->WaitForPendingWrites() == FileError::kSuccess);
        dev->GetAsyncIOStats(wall, count, minUs, maxUs, avgUs);
        // Not "exactly one": the backend is free to satisfy a queued write
        // synchronously, and then there is no asynchronous write to count. What
        // has to hold either way is that the distribution is self-consistent --
        // a max below a min would mean the counters were never really written.
        if (count > 0)
            CHECK(maxUs >= minUs);
    }
}

// ============================================================================
// Retry policy
// ============================================================================

TEST_CASE("A failed open of a file is not worth retrying", "[fileops-win]")
{
    // The distinction that cost sixty-four seconds: a physical drive may be
    // briefly held by the OS after a dismount, so a refused open there is worth
    // another attempt. A file that refuses an open will refuse it again.
    auto ops = FileOperations::Create();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("plain.img"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1024, '\0'));
    }
    CHECK_FALSE(ops->OpenFailureMayBeTransient(path.toStdString()));
}

// ============================================================================
// Against a real physical drive
// ============================================================================

TEST_CASE("A physical drive opens and reports the size the OS gives it",
          "[fileops-win][vhd]")
{
    rpi_test::VhdDevice vhd(64);
    if (!vhd.valid())
        SKIP("no virtual disk: " + vhd.reason().toStdString());

    auto ops = FileOperations::Create();
    if (ops->OpenDevice(vhd.path().toStdString()) != FileError::kSuccess)
        SKIP("the attached disk could not be opened for writing");

    std::uint64_t size = 0;
    REQUIRE(ops->GetSize(size) == FileError::kSuccess);
    // Read from the device rather than from a QFileInfo: this is the number the
    // capacity check is made against, and a file length is not it.
    CHECK(size >= 60u * 1024 * 1024);
    CHECK(ops->Close() == FileError::kSuccess);
}

// A holder that looks like an indexer or a scanner: write access, shared for
// reading only, so an open asking for write is refused.
namespace {
class ExclusiveHolder
{
public:
    explicit ExclusiveHolder(const QString &devicePath)
    {
        const std::wstring wide = devicePath.toStdWString();
        _handle = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    ~ExclusiveHolder()
    {
        if (_handle != INVALID_HANDLE_VALUE)
            CloseHandle(_handle);
    }
    ExclusiveHolder(const ExclusiveHolder &) = delete;
    ExclusiveHolder &operator=(const ExclusiveHolder &) = delete;
    bool held() const { return _handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE _handle = INVALID_HANDLE_VALUE;
};

qint64 millisecondsFor(const std::function<void()> &work)
{
    const auto started = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started)
        .count();
}
} // namespace

TEST_CASE("A drive something else holds is waited for, not given up on",
          "[fileops-win][vhd]")
{
    // A just-written removable disk is often held for a moment by Explorer
    // re-scanning it, an anti-virus scanner or the search indexer. Dropping
    // straight to a shared open would let Windows mount the partition about to
    // be written and raise "You need to format the disk" mid-write, so the
    // open backs off and asks again before it settles for sharing.
    rpi_test::VhdDevice vhd(64);
    if (!vhd.valid())
        SKIP("attaching a virtual disk needs elevation: " + vhd.reason().toStdString());

    ExclusiveHolder holder(vhd.path());
    REQUIRE(holder.held());

    auto ops = FileOperations::Create();
    FileError err = FileError::kSuccess;
    const qint64 ms = millisecondsFor([&] {
        err = ops->OpenDevice(vhd.path().toStdString());
    });

    // The holder never lets go, so even the shared fallback is refused.
    CHECK(err != FileError::kSuccess);
    CHECK_FALSE(ops->IsOpen());
    // And it was not refused at once: the six backoffs are a little over six
    // seconds all told, so anything under five means the loop was skipped.
    INFO("open took " << ms << "ms");
    CHECK(ms >= 5000);
}

TEST_CASE("An open waiting for a drive gives up when cancelled",
          "[fileops-win][vhd]")
{
    // The backoff above outlasts a caller that has already pressed Cancel, so
    // it is checked between sleeps rather than only between attempts.
    rpi_test::VhdDevice vhd(64);
    if (!vhd.valid())
        SKIP("attaching a virtual disk needs elevation: " + vhd.reason().toStdString());

    ExclusiveHolder holder(vhd.path());
    REQUIRE(holder.held());

    auto ops = FileOperations::Create();
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ops->CancelAsyncIO();
    });

    FileError err = FileError::kSuccess;
    const qint64 ms = millisecondsFor([&] {
        err = ops->OpenDevice(vhd.path().toStdString());
    });
    canceller.join();

    CHECK(err != FileError::kSuccess);
    INFO("open took " << ms << "ms");
    CHECK(ms < 5000);
}

TEST_CASE("A failed open of a physical drive is worth retrying",
          "[fileops-win][vhd]")
{
    rpi_test::VhdDevice vhd(64);
    if (!vhd.valid())
        SKIP("no virtual disk: " + vhd.reason().toStdString());

    // The answer comes from the error the last open failed with, so there has
    // to have been one. Asked of a backend that has never tried, it correctly
    // says no -- which is what this case used to do, and it proved nothing.
    ExclusiveHolder holder(vhd.path());
    REQUIRE(holder.held());

    auto ops = FileOperations::Create();
    REQUIRE(ops->OpenDevice(vhd.path().toStdString()) != FileError::kSuccess);

    // A drive held by something else is worth trying again; the holder is
    // usually an indexer or a scanner and lets go by itself.
    CHECK(ops->OpenFailureMayBeTransient(vhd.path().toStdString()));

    // Where a file is not, whatever it failed with.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString missing = QDir(dir.path()).filePath(QStringLiteral("not-here.img"));
    auto fileOps = FileOperations::Create();
    REQUIRE(fileOps->OpenDevice(missing.toStdString()) != FileError::kSuccess);
    CHECK_FALSE(fileOps->OpenFailureMayBeTransient(missing.toStdString()));
}

// ============================================================================
// A device that refuses writes while it settles
//
// After the partition table is rewritten, Windows re-enumerates the disk and
// the medium answers ERROR_NOT_READY for a short while. The open is unaffected
// -- it never touches the medium -- so the refusal lands on the first write of
// the image. These cases hold the backend to reissuing that write rather than
// failing the whole card, and to giving up where reissuing is pointless.
// ============================================================================

#ifdef FILEOPS_ENABLE_TEST_API

namespace {

// The fault-injection seam is on the Windows backend, not on the interface.
rpi_imager::WindowsFileOperations &backend(const ScratchDevice &dev)
{
    return *static_cast<rpi_imager::WindowsFileOperations *>(dev.operator->());
}

// A queued write, and what its completion callback was told.
struct QueuedWrite {
    FileError result = FileError::kWriteError;  // anything but kSuccess
    std::uint64_t bytes = 0;
};

} // namespace

TEST_CASE("A write refused while the device settles is issued again",
          "[fileops-win][transient]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");
    dev->SetAsyncQueueDepth(4);

    // Three refusals, then the device comes back -- which is the shape of a
    // re-enumeration rather than a broken card.
    backend(dev).FailNextWriteCompletions(ERROR_NOT_READY, 3);

    const auto data = pattern(32 * 1024, 7);
    QueuedWrite w;
    REQUIRE(dev->AsyncWriteSequential(
                data.data(), data.size(),
                [&](FileError e, std::uint64_t n) { w.result = e; w.bytes = n; }) ==
            FileError::kSuccess);

    REQUIRE(dev->WaitForPendingWrites() == FileError::kSuccess);
    CHECK(backend(dev).RemainingInjectedWriteFailures() == 0);
    CHECK(w.result == FileError::kSuccess);
    CHECK(w.bytes == data.size());

    // The point of the exercise: the bytes are on the device, written once and
    // in the right place, despite the refusals.
    REQUIRE(dev->Seek(0) == FileError::kSuccess);
    std::vector<std::uint8_t> got(data.size());
    std::size_t read = 0;
    REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
    CHECK(read == data.size());
    CHECK(got == data);
}

TEST_CASE("A write refused for a reason that will not change is not reissued",
          "[fileops-win][transient]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");
    dev->SetAsyncQueueDepth(4);

    // A protected medium answers the same however often it is asked. Reissuing
    // would only delay the report the user is waiting for.
    backend(dev).FailNextWriteCompletions(ERROR_WRITE_PROTECT, 1);

    const auto data = pattern(8192, 13);
    QueuedWrite w;
    REQUIRE(dev->AsyncWriteSequential(
                data.data(), data.size(),
                [&](FileError e, std::uint64_t n) { w.result = e; w.bytes = n; }) ==
            FileError::kSuccess);

    CHECK(dev->WaitForPendingWrites() == FileError::kWriteError);
    CHECK(w.result == FileError::kWriteError);
    // Refused on the first attempt, so the armed failure was spent exactly once.
    CHECK(backend(dev).RemainingInjectedWriteFailures() == 0);
}

TEST_CASE("Reissues are bounded, and the write fails once they run out",
          "[fileops-win][transient]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");
    dev->SetAsyncQueueDepth(4);

    // One more refusal than there are attempts: a device that never comes back
    // has to be reported rather than retried forever. Costs the full backoff,
    // which is what bounds it.
    backend(dev).FailNextWriteCompletions(
        ERROR_NOT_READY, rpi_imager::TimeoutDefaults::kTransientWriteRetries + 1);

    const auto data = pattern(4096, 17);
    QueuedWrite w;
    REQUIRE(dev->AsyncWriteSequential(
                data.data(), data.size(),
                [&](FileError e, std::uint64_t n) { w.result = e; w.bytes = n; }) ==
            FileError::kSuccess);

    CHECK(dev->WaitForPendingWrites() == FileError::kWriteError);
    CHECK(w.result == FileError::kWriteError);
    CHECK(backend(dev).RemainingInjectedWriteFailures() == 0);
}

TEST_CASE("A refusal reaches the reissue path from the queue as well as the drain",
          "[fileops-win][transient]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");

    // A shallow queue makes the writer collect completions as it goes, rather
    // than only when it drains at the end -- the two places a completion is
    // read, and so the two that have to agree about reissuing.
    dev->SetAsyncQueueDepth(2);
    backend(dev).FailNextWriteCompletions(ERROR_NOT_READY, 2);

    const auto block = pattern(4096, 23);
    std::vector<QueuedWrite> writes(6);
    for (auto &w : writes) {
        REQUIRE(dev->AsyncWriteSequential(
                    block.data(), block.size(),
                    [&w](FileError e, std::uint64_t n) { w.result = e; w.bytes = n; }) ==
                FileError::kSuccess);
    }

    REQUIRE(dev->WaitForPendingWrites() == FileError::kSuccess);
    CHECK(backend(dev).RemainingInjectedWriteFailures() == 0);
    for (const auto &w : writes) {
        CHECK(w.result == FileError::kSuccess);
        CHECK(w.bytes == block.size());
    }

    // Six blocks of the same content, so a reissue that landed at the wrong
    // offset shows up as a short or mismatched read rather than being masked.
    REQUIRE(dev->Seek(0) == FileError::kSuccess);
    std::vector<std::uint8_t> got(block.size() * writes.size());
    std::size_t read = 0;
    REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
    CHECK(read == got.size());
    for (std::size_t i = 0; i < writes.size(); ++i) {
        std::vector<std::uint8_t> slice(got.begin() + i * block.size(),
                                        got.begin() + (i + 1) * block.size());
        CHECK(slice == block);
    }
}

// ============================================================================
// Refused synchronous writes
// ============================================================================
// WriteAtOffset is what the write path falls back to where the asynchronous
// queue is not in use, and it sorts the failures it gets into ones worth
// trying again and ones that will never come right. None of that had run:
// a scratch file does not answer "write protected" on request, so every
// branch below the first was reached only on somebody's card, in the field,
// with the card already spoilt.

namespace {

// A write of one block to a device that answers with `error` `count` times.
FileError writeRefusedWith(ScratchDevice &dev, unsigned long error, int count)
{
    backend(dev).FailNextWriteCompletions(error, count);
    const auto block = pattern(4096, 7);
    return dev->WriteAtOffset(0, block.data(), block.size());
}

} // namespace

TEST_CASE("A write refused for a reason that will not change is not retried",
          "[fileops-win][transient]")
{
    // A full disk, a write-protected card and a bad sector are all answers
    // the next attempt gets as well. Retrying them costs the user three more
    // seconds and the same failure, and on a card that is failing it is
    // three more attempts at a sector that is going.
    const unsigned long error = GENERATE(
        static_cast<unsigned long>(ERROR_DISK_FULL),
        static_cast<unsigned long>(ERROR_WRITE_PROTECT),
        static_cast<unsigned long>(ERROR_SECTOR_NOT_FOUND),
        static_cast<unsigned long>(ERROR_CRC),
        static_cast<unsigned long>(ERROR_ACCESS_DENIED));

    ScratchDevice dev;
    INFO("error " << error);
    // One injection, and it is not used up by a retry -- because there is
    // none.
    CHECK(writeRefusedWith(dev, error, 4) == FileError::kWriteError);
    CHECK(backend(dev).RemainingInjectedWriteFailures() == 3);
}

TEST_CASE("A write refused while the device settles is tried again",
          "[fileops-win][transient]")
{
    // ERROR_NOT_READY is what a card reader answers while Windows finishes
    // re-enumerating the disk after the partition table is rewritten. It
    // comes right on its own, so the write is reissued rather than the card
    // being called a failure.
    ScratchDevice dev;
    CHECK(writeRefusedWith(dev, ERROR_NOT_READY, 1) == FileError::kSuccess);
    CHECK(backend(dev).RemainingInjectedWriteFailures() == 0);

    // And what it wrote is what was asked for, at the offset that was asked
    // for -- a retry that lost its place would pass the return code alone.
    const auto block = pattern(4096, 7);
    std::vector<std::uint8_t> got(block.size());
    REQUIRE(dev->Seek(0) == FileError::kSuccess);
    std::size_t read = 0;
    REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
    CHECK(read == got.size());
    CHECK(got == block);
}

TEST_CASE("A write refused past the attempts allowed gives up",
          "[fileops-win][transient]")
{
    // Three retries, then the error is reported. Carrying on would be a
    // write that never returns and a progress bar that never moves.
    ScratchDevice dev;
    CHECK(writeRefusedWith(dev, ERROR_NOT_READY, 10) == FileError::kWriteError);
    // Four attempts in total: the first and three retries.
    CHECK(backend(dev).RemainingInjectedWriteFailures() == 6);
}

TEST_CASE("A refused write says what refused it", "[fileops-win][transient]")
{
    // The classification behind the message the user is shown. It reads the
    // last error the backend recorded, and a synchronous write that failed
    // used not to record one -- so a write-protected card was reported from
    // whatever had been stored several steps earlier, usually the open.
    struct Case {
        unsigned long error;
        rpi_imager::WriteErrorClass expected;
        const char *why;
    };
    static const Case kCases[] = {
        {ERROR_DISK_FULL, rpi_imager::WriteErrorClass::kDiskFull, "no room left"},
        {ERROR_WRITE_PROTECT, rpi_imager::WriteErrorClass::kWriteProtected,
         "the lock switch on the card"},
        {ERROR_CRC, rpi_imager::WriteErrorClass::kMediaError, "a bad sector"},
        {ERROR_SECTOR_NOT_FOUND, rpi_imager::WriteErrorClass::kMediaError,
         "a sector that is not there"},
        {ERROR_INVALID_PARAMETER, rpi_imager::WriteErrorClass::kInvalidParameter,
         "an unaligned transfer"},
        {ERROR_IO_DEVICE, rpi_imager::WriteErrorClass::kIoDeviceError,
         "the card pulled out mid-write"},
    };
    // ERROR_ACCESS_DENIED is deliberately not in the table: what it means
    // depends on whether Defender's Controlled Folder Access is blocking disk
    // writes on this machine, and asserting one answer would fail on a
    // machine set the other way. Both answers are pinned in the Defender
    // cases below.

    for (const Case &c : kCases) {
        ScratchDevice dev;
        INFO(c.why << " (error " << c.error << ")");
        // More than the attempts allowed, so the errors that are retried run
        // out of attempts rather than succeeding on the next one.
        REQUIRE(writeRefusedWith(dev, c.error, 8) == FileError::kWriteError);
        CHECK(backend(dev).GetLastErrorCode() == static_cast<int>(c.error));
        CHECK(backend(dev).ClassifyLastWriteError() == c.expected);
    }
}

TEST_CASE("An error with no category of its own is not given one",
          "[fileops-win][transient]")
{
    // Better an unspecific message than a confident wrong one: telling
    // somebody their card is write-protected when it is not sends them
    // looking at a switch that was never the problem.
    ScratchDevice dev;
    REQUIRE(writeRefusedWith(dev, ERROR_NOT_SUPPORTED, 8) == FileError::kWriteError);
    CHECK(backend(dev).ClassifyLastWriteError() == rpi_imager::WriteErrorClass::kUnknown);
}

TEST_CASE("A write that came right on a retry leaves no error behind",
          "[fileops-win][transient]")
{
    // A refusal it recovered from is not an error.
    ScratchDevice dev;
    REQUIRE(writeRefusedWith(dev, ERROR_NOT_READY, 1) == FileError::kSuccess);
    CHECK(backend(dev).GetLastErrorCode() == 0);
    CHECK(backend(dev).ClassifyLastWriteError() == rpi_imager::WriteErrorClass::kUnknown);
}

// ============================================================================
// Refused asynchronous writes
// ============================================================================
// Async failures recorded no Win32 error, so the user was never told why.

namespace {

// One write never fills the queue, so the drain reads its refusal.
void refuseInTheDrain(ScratchDevice &dev, unsigned long error)
{
    dev->SetAsyncQueueDepth(4);
    backend(dev).FailNextWriteCompletions(error, 1);
    const auto data = pattern(8192, 29);
    REQUIRE(dev->AsyncWriteSequential(data.data(), data.size(), nullptr) ==
            FileError::kSuccess);
    REQUIRE(dev->WaitForPendingWrites() == FileError::kWriteError);
}

} // namespace

TEST_CASE("An async write refused in the drain says what refused it",
          "[fileops-win][transient]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");
    refuseInTheDrain(dev, ERROR_WRITE_PROTECT);
    CHECK(backend(dev).GetLastErrorCode() == static_cast<int>(ERROR_WRITE_PROTECT));
    CHECK(backend(dev).ClassifyLastWriteError() ==
          rpi_imager::WriteErrorClass::kWriteProtected);
}

TEST_CASE("An async write refused while the queue is full says what refused it",
          "[fileops-win][transient]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");

    // Two slots, so the third write collects the refusal before the drain.
    dev->SetAsyncQueueDepth(2);
    backend(dev).FailNextWriteCompletions(ERROR_CRC, 1);

    const auto block = pattern(4096, 31);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(dev->AsyncWriteSequential(block.data(), block.size(), nullptr) ==
                FileError::kSuccess);
    }
    // Refused, so the queue saw the failure first.
    CHECK(dev->AsyncWriteSequential(block.data(), block.size(), nullptr) ==
          FileError::kWriteError);

    CHECK(dev->WaitForPendingWrites() == FileError::kWriteError);
    CHECK(backend(dev).GetLastErrorCode() == static_cast<int>(ERROR_CRC));
    CHECK(backend(dev).ClassifyLastWriteError() == rpi_imager::WriteErrorClass::kMediaError);
}

TEST_CASE("A write that succeeds after an async refusal does not hide it",
          "[fileops-win][transient]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");
    refuseInTheDrain(dev, ERROR_WRITE_PROTECT);

    const auto more = pattern(4096, 37);
    REQUIRE(dev->WriteSequential(more.data(), more.size()) == FileError::kSuccess);
    CHECK(backend(dev).GetLastErrorCode() == static_cast<int>(ERROR_WRITE_PROTECT));
    CHECK(backend(dev).ClassifyLastWriteError() ==
          rpi_imager::WriteErrorClass::kWriteProtected);
}

TEST_CASE("A cancelled device abandons a write it was retrying",
          "[fileops-win][transient]")
{
    // Cancelling is checked at the top of every attempt, so a retry loop
    // does not hold a cancel up for its whole backoff.
    ScratchDevice dev;
    dev->CancelAsyncIO();
    const auto block = pattern(4096, 7);
    CHECK(dev->WriteAtOffset(0, block.data(), block.size()) == FileError::kCancelled);
}


TEST_CASE("The emergency replay puts every pending write where it belongs",
          "[fileops-win][transient]")
{
    // Reached in the product from a five-minute timeout, when writes have
    // stopped completing entirely. What matters is that each outstanding
    // buffer is written at the offset it was given rather than wherever the
    // file pointer happened to be, because the queue is replayed in order
    // after the async attempt is abandoned.
    ScratchDevice dev(8u * 1024 * 1024);
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");

    dev->SetAsyncQueueDepth(32);

    // Three distinguishable blocks, queued back to back so they take
    // consecutive offsets from nought.
    std::vector<std::vector<std::uint8_t>> blocks;
    for (int i = 0; i < 3; ++i)
        blocks.push_back(pattern(64 * 1024, static_cast<std::uint8_t>(100 + i)));
    for (const auto &b : blocks) {
        REQUIRE(dev->AsyncWriteSequential(b.data(), b.size()) == FileError::kSuccess);
    }

    REQUIRE(backend(dev).ReplayPendingWritesSynchronously() == FileError::kSuccess);
    CHECK(dev->IsInSyncFallbackMode());

    // Whether they were replayed or had already landed, the file must read
    // back as the three blocks in the order they were queued.
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        REQUIRE(dev->Seek(static_cast<std::uint64_t>(i) * 64 * 1024) == FileError::kSuccess);
        std::vector<std::uint8_t> got(blocks[i].size());
        std::size_t read = 0;
        REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
        INFO("block " << i);
        CHECK(got == blocks[i]);
    }
}

#endif // FILEOPS_ENABLE_TEST_API

// ============================================================================
// Where a synchronous write lands after asynchronous ones
// ============================================================================

TEST_CASE("A synchronous write continues where the asynchronous queue stopped",
          "[fileops-win]")
{
    ScratchDevice dev;
    if (!dev->IsAsyncIOSupported())
        SKIP("this backend has no asynchronous path to exercise");
    dev->SetAsyncQueueDepth(4);

    // The sequence a failed async write puts the backend through: queue
    // asynchronously, then carry on synchronously. The synchronous write used
    // to take its offset from the position only reads and Seek() ever moved,
    // so it rewound to the last seek and overwrote what had just been written.
    const auto first = pattern(8192, 41);
    REQUIRE(dev->AsyncWriteSequential(first.data(), first.size()) == FileError::kSuccess);
    REQUIRE(dev->WaitForPendingWrites() == FileError::kSuccess);

    const auto second = pattern(8192, 43);
    REQUIRE(dev->WriteSequential(second.data(), second.size()) == FileError::kSuccess);

    REQUIRE(dev->Seek(0) == FileError::kSuccess);
    std::vector<std::uint8_t> got(first.size() + second.size());
    std::size_t read = 0;
    REQUIRE(dev->ReadSequential(got.data(), got.size(), read) == FileError::kSuccess);
    REQUIRE(read == got.size());

    std::vector<std::uint8_t> head(got.begin(), got.begin() + first.size());
    std::vector<std::uint8_t> tail(got.begin() + first.size(), got.end());
    CHECK(head == first);
    CHECK(tail == second);
}

// ============================================================================
// Windows Defender Controlled Folder Access
// ============================================================================
// Defender can stop an application writing, and the write fails with a plain
// ERROR_ACCESS_DENIED -- the same code as a permissions problem the user
// could actually fix. Told apart, the remedy is to add Imager to an
// allow-list; not told apart, the user goes hunting for a file permission
// that was never the problem.
//
// It has five modes, not two:
//
//   0  disabled
//   1  enabled                       blocks protected folders and disk sectors
//   2  audit mode                    logs, blocks nothing
//   3  block disk modification only  blocks disk sectors
//   4  audit disk modification only  logs disk-sector writes, blocks nothing
//
// https://learn.microsoft.com/en-us/defender-endpoint/controlled-folder-access-configure
//
// Mode 3 is the one aimed squarely at what a disk imager does, and only mode
// 1 used to count -- so a user in mode 3 was blocked by Defender and told it
// was a permissions problem.
//
// Turning it on from a test is not on: it is machine policy, and it would
// block whatever else the machine is doing. The decision is reached directly
// instead.

#ifdef FILEOPS_ENABLE_TEST_API

// Declared inside the backend's own namespace, which is where the file that
// defines them lives.
namespace rpi_imager {
namespace WindowsWriteErrorTesting {
bool cfaModeBlocks(unsigned long mode);
int classify(unsigned long error, bool defenderBlocks);
bool defenderBlocksDiskWritesHere();
}
}
using namespace rpi_imager::WindowsWriteErrorTesting;

TEST_CASE("Only the modes that block a disk write count as blocking",
          "[fileops-win][defender]")
{

    CHECK_FALSE(cfaModeBlocks(0));   // disabled
    CHECK(cfaModeBlocks(1));         // enabled
    CHECK_FALSE(cfaModeBlocks(2));   // audit mode: logs, blocks nothing
    // Block disk modification only. This is the one that catches an imager,
    // and the one that used to be read as "not enabled".
    CHECK(cfaModeBlocks(3));
    CHECK_FALSE(cfaModeBlocks(4));   // audit disk modification only

    // A mode from a newer Defender than this was built against. Claiming it
    // blocks would blame Defender for every access denial on that machine.
    CHECK_FALSE(cfaModeBlocks(5));
    CHECK_FALSE(cfaModeBlocks(99));
}

TEST_CASE("Defender blocking a write is told apart from a permissions problem",
          "[fileops-win][defender]")
{

    const int denied = static_cast<int>(rpi_imager::WriteErrorClass::kAccessDenied);
    const int byDefender = static_cast<int>(
        rpi_imager::WriteErrorClass::kAccessDeniedControlledFolderAccess);
    REQUIRE(denied != byDefender);

    CHECK(classify(ERROR_ACCESS_DENIED, true) == byDefender);
    CHECK(classify(ERROR_ACCESS_DENIED, false) == denied);
}

TEST_CASE("Defender is not blamed for errors it has no part in",
          "[fileops-win][defender]")
{
    // Only an access denial can be Defender's doing. A full disk is a full
    // disk whatever Defender is set to, and sending that user to an
    // allow-list wastes their time.

    for (unsigned long error : {static_cast<unsigned long>(ERROR_DISK_FULL),
                                static_cast<unsigned long>(ERROR_WRITE_PROTECT),
                                static_cast<unsigned long>(ERROR_CRC),
                                static_cast<unsigned long>(ERROR_SECTOR_NOT_FOUND),
                                static_cast<unsigned long>(ERROR_INVALID_PARAMETER),
                                static_cast<unsigned long>(ERROR_IO_DEVICE),
                                static_cast<unsigned long>(ERROR_NOT_SUPPORTED)}) {
        INFO("error " << error);
        CHECK(classify(error, true) == classify(error, false));
        CHECK(classify(error, true) !=
              static_cast<int>(
                  rpi_imager::WriteErrorClass::kAccessDeniedControlledFolderAccess));
    }
}

TEST_CASE("Asking what Defender is set to answers without faulting",
          "[fileops-win][defender]")
{
    // Read-only, and what it answers depends on the machine -- so this asks
    // only that the registry read runs to the end, releases what it took, and
    // gives the same answer twice.
    const bool first = defenderBlocksDiskWritesHere();
    const bool second = defenderBlocksDiskWritesHere();
    INFO("Defender blocks disk writes on this machine: " << first);
    CHECK(first == second);
}

#endif // FILEOPS_ENABLE_TEST_API

// ---------------------------------------------------------------------------
// Emulating Controlled Folder Access being on
// ---------------------------------------------------------------------------
// The real setting is under HKEY_LOCAL_MACHINE. A test has no business
// writing it: it is machine security policy, and turning Controlled Folder
// Access on would block whatever else the machine is doing. So the probe is
// pointed at a key under HKEY_CURRENT_USER, which this process may write
// freely, and the reading is exercised for real -- each mode, a value of the
// wrong type, a missing value, and a missing key.

namespace rpi_imager {
namespace WindowsWriteErrorTesting {
void readCfaFrom(void *root, const char *subkey);
long cfaModeRead();
}
}

namespace {

// A scratch registry key that removes itself.
class ScratchRegistryKey
{
public:
    ScratchRegistryKey()
        : _path("Software\rpi-imager-test\\" +
                QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString())
    {
        HKEY key = nullptr;
        _made = ::RegCreateKeyExA(HKEY_CURRENT_USER, _path.c_str(), 0, nullptr,
                                  REG_OPTION_VOLATILE, KEY_READ | KEY_WRITE,
                                  nullptr, &key, nullptr) == ERROR_SUCCESS;
        if (_made)
            ::RegCloseKey(key);
    }

    ~ScratchRegistryKey()
    {
        rpi_imager::WindowsWriteErrorTesting::readCfaFrom(nullptr, nullptr);
        if (_made)
            ::RegDeleteKeyA(HKEY_CURRENT_USER, _path.c_str());
    }

    ScratchRegistryKey(const ScratchRegistryKey &) = delete;
    ScratchRegistryKey &operator=(const ScratchRegistryKey &) = delete;

    bool ok() const { return _made; }
    const std::string &path() const { return _path; }

    // Point the probe here, so it reads what this test writes.
    void useForCfa() const
    {
        rpi_imager::WindowsWriteErrorTesting::readCfaFrom(HKEY_CURRENT_USER,
                                                          _path.c_str());
    }

    bool setDword(const char *name, DWORD value) const
    {
        HKEY key = nullptr;
        if (::RegOpenKeyExA(HKEY_CURRENT_USER, _path.c_str(), 0, KEY_WRITE, &key)
            != ERROR_SUCCESS)
            return false;
        const LONG rc = ::RegSetValueExA(key, name, 0, REG_DWORD,
                                         reinterpret_cast<const BYTE *>(&value),
                                         sizeof(value));
        ::RegCloseKey(key);
        return rc == ERROR_SUCCESS;
    }

    bool setString(const char *name, const char *value) const
    {
        HKEY key = nullptr;
        if (::RegOpenKeyExA(HKEY_CURRENT_USER, _path.c_str(), 0, KEY_WRITE, &key)
            != ERROR_SUCCESS)
            return false;
        const LONG rc = ::RegSetValueExA(
            key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value),
            static_cast<DWORD>(std::strlen(value) + 1));
        ::RegCloseKey(key);
        return rc == ERROR_SUCCESS;
    }

private:
    std::string _path;
    bool _made = false;
};

} // namespace

TEST_CASE("Every Controlled Folder Access mode is read back as it was written",
          "[fileops-win][defender]")
{
    ScratchRegistryKey scratch;
    if (!scratch.ok())
        SKIP("could not create a scratch registry key to read the setting from");
    scratch.useForCfa();

    for (DWORD mode = 0; mode <= 4; ++mode) {
        INFO("mode " << mode);
        REQUIRE(scratch.setDword("EnableControlledFolderAccess", mode));
        CHECK(rpi_imager::WindowsWriteErrorTesting::cfaModeRead() ==
              static_cast<long>(mode));
        CHECK(rpi_imager::WindowsWriteErrorTesting::defenderBlocksDiskWritesHere() ==
              (mode == 1 || mode == 3));
    }
}

TEST_CASE("Controlled Folder Access switched on is reported as the cause",
          "[fileops-win][defender]")
{
    // The end of the story: the setting is on, a write comes back access
    // denied, and the user is told it was Defender rather than sent looking
    // for a file permission.
    ScratchRegistryKey scratch;
    if (!scratch.ok())
        SKIP("could not create a scratch registry key to read the setting from");
    scratch.useForCfa();
    REQUIRE(scratch.setDword("EnableControlledFolderAccess", 1));

    ScratchDevice dev;
    REQUIRE(writeRefusedWith(dev, ERROR_ACCESS_DENIED, 8) == FileError::kWriteError);
    CHECK(backend(dev).ClassifyLastWriteError() ==
          rpi_imager::WriteErrorClass::kAccessDeniedControlledFolderAccess);
}

TEST_CASE("Controlled Folder Access switched off leaves a plain denial alone",
          "[fileops-win][defender]")
{
    ScratchRegistryKey scratch;
    if (!scratch.ok())
        SKIP("could not create a scratch registry key to read the setting from");
    scratch.useForCfa();
    REQUIRE(scratch.setDword("EnableControlledFolderAccess", 0));

    ScratchDevice dev;
    REQUIRE(writeRefusedWith(dev, ERROR_ACCESS_DENIED, 8) == FileError::kWriteError);
    CHECK(backend(dev).ClassifyLastWriteError() ==
          rpi_imager::WriteErrorClass::kAccessDenied);
}

TEST_CASE("A setting that is not there, or not a number, is not read as on",
          "[fileops-win][defender]")
{
    // The ordinary case is the key missing entirely -- every machine that has
    // never turned it on. Reading that as enabled would blame Defender for
    // every access denial there is.
    ScratchRegistryKey scratch;
    if (!scratch.ok())
        SKIP("could not create a scratch registry key to read the setting from");
    scratch.useForCfa();

    // Key there, value absent.
    CHECK(rpi_imager::WindowsWriteErrorTesting::cfaModeRead() == -1);
    CHECK_FALSE(rpi_imager::WindowsWriteErrorTesting::defenderBlocksDiskWritesHere());

    // Value there, but a string rather than the DWORD it must be.
    REQUIRE(scratch.setString("EnableControlledFolderAccess", "1"));
    CHECK(rpi_imager::WindowsWriteErrorTesting::cfaModeRead() == -1);
    CHECK_FALSE(rpi_imager::WindowsWriteErrorTesting::defenderBlocksDiskWritesHere());

    // And no key at all.
    rpi_imager::WindowsWriteErrorTesting::readCfaFrom(
        HKEY_CURRENT_USER, "Software\rpi-imager-test\definitely-not-here");
    CHECK(rpi_imager::WindowsWriteErrorTesting::cfaModeRead() == -1);
    CHECK_FALSE(rpi_imager::WindowsWriteErrorTesting::defenderBlocksDiskWritesHere());
}
