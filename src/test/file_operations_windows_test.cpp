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

#include <memory>
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

TEST_CASE("A failed open of a physical drive is worth retrying",
          "[fileops-win][vhd]")
{
    rpi_test::VhdDevice vhd(64);
    if (!vhd.valid())
        SKIP("no virtual disk: " + vhd.reason().toStdString());

    // The other half of the retry decision: a drive is classified as retryable
    // where a file is not.
    auto ops = FileOperations::Create();
    const bool retryable = ops->OpenFailureMayBeTransient(vhd.path().toStdString());
    CHECK(retryable);
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
// The synchronous write, refused
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
