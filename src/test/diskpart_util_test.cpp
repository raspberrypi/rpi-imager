/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * DiskpartUtil: the Windows path that wipes a card before it is written.
 *
 * None of this had a test. The suite compiled diskpart_util.cpp into nothing
 * at all until recently, so the code that decides which disk `clean` is aimed
 * at, and the code that locks and dismounts volumes before a raw write, ran
 * only in production.
 *
 * Two kinds of case here. The parsing cases need no privilege and run
 * everywhere: they pin the decision about which disk number a device path
 * means, which is the difference between wiping the card and wiping the
 * machine. The device cases need an attached VHD, and skip without one.
 *
 * Nothing here is ever pointed at a real drive. Every destructive call goes
 * through the VHD's own path and is checked against it first.
 */

#include <catch2/catch_test_macros.hpp>

#include "windows/diskpart_util.h"
#include "vhd_device.h"
#include "platform_privilege.h"

#include <QByteArray>
#include <QString>

// Declared in diskpart_util.cpp when the test API is enabled.
namespace DiskpartUtil {
namespace TestAPI {
    bool extractDiskNumber(const QByteArray &device, int &diskNumber);
}
}

using DiskpartUtil::TestAPI::extractDiskNumber;

// ============================================================================
// Which disk does this path mean?
// ============================================================================

TEST_CASE("A physical drive path yields its disk number", "[diskpart][parse]")
{
    int n = -1;
    REQUIRE(extractDiskNumber(QByteArrayLiteral("\\\\.\\PHYSICALDRIVE0"), n));
    CHECK(n == 0);

    n = -1;
    REQUIRE(extractDiskNumber(QByteArrayLiteral("\\\\.\\PHYSICALDRIVE7"), n));
    CHECK(n == 7);

    // Two digits: the machines this runs on do reach disk 10 and beyond once a
    // card reader, a phone and a couple of USB sticks are plugged in.
    n = -1;
    REQUIRE(extractDiskNumber(QByteArrayLiteral("\\\\.\\PHYSICALDRIVE13"), n));
    CHECK(n == 13);
}

TEST_CASE("The drive path is matched whatever case it is written in",
          "[diskpart][parse]")
{
    // Drivelist and the Windows APIs disagree about capitalisation --
    // GetVirtualDiskPhysicalPath hands back \\.\PhysicalDrive2 -- and the
    // number has to come out the same either way.
    int n = -1;
    REQUIRE(extractDiskNumber(QByteArrayLiteral("\\\\.\\PhysicalDrive2"), n));
    CHECK(n == 2);

    n = -1;
    REQUIRE(extractDiskNumber(QByteArrayLiteral("\\\\.\\physicaldrive2"), n));
    CHECK(n == 2);
}

TEST_CASE("A path that is not a physical drive yields no disk number",
          "[diskpart][parse]")
{
    int n = 99;

    // A volume, not a disk. `clean` against the disk number behind a volume
    // path would wipe the whole card rather than the partition asked for.
    CHECK_FALSE(extractDiskNumber(QByteArrayLiteral("\\\\.\\C:"), n));

    // POSIX paths, which reach this code when a Linux device string is carried
    // through a shared code path by mistake.
    CHECK_FALSE(extractDiskNumber(QByteArrayLiteral("/dev/sda"), n));
    CHECK_FALSE(extractDiskNumber(QByteArrayLiteral("/dev/mmcblk0"), n));

    CHECK_FALSE(extractDiskNumber(QByteArray(), n));
    CHECK_FALSE(extractDiskNumber(QByteArrayLiteral("PHYSICALDRIVE0"), n));

    // A partition on the drive, not the drive. std::regex_match wants the whole
    // string, so the trailing text has to be refused rather than ignored.
    CHECK_FALSE(extractDiskNumber(QByteArrayLiteral("\\\\.\\PHYSICALDRIVE0p1"), n));

    // Nothing above should have touched the out parameter.
    CHECK(n == 99);
}

TEST_CASE("A disk number too large to hold is refused, not thrown",
          "[diskpart][parse]")
{
    // The capture is ([0-9]+) with no bound, and the conversion is std::stoi,
    // which throws std::out_of_range past INT_MAX. Reaching this from a device
    // path is far-fetched, but the answer to a path it cannot parse has to be
    // "no" rather than an exception unwinding out of a disk-wiping routine.
    int n = 42;
    CHECK_FALSE(extractDiskNumber(
        QByteArrayLiteral("\\\\.\\PHYSICALDRIVE99999999999999999999"), n));
    CHECK(n == 42);
}

// ============================================================================
// Against a real attached disk
// ============================================================================

#define REQUIRE_VHD(vhd)                                                       \
    if (!(vhd).valid())                                                        \
    SKIP("no virtual disk: " + (vhd).reason().toStdString())

namespace {

// The only way a case here reaches a DiskpartUtil call.
//
// These functions unmount volumes and wipe partition tables, and the path
// they take is one character away from a real drive. Taking the object rather
// than a path means a case cannot name a disk at all: it gets the one that
// was attached for it, checked, or the assertion fails before anything is
// touched. A path pasted in from a bug report will not compile.
template <typename Call>
auto onOurDisk(const rpi_test::VhdDevice &vhd, Call call) -> decltype(call(QByteArray()))
{
    const QByteArray device = vhd.pathBytes();
    REQUIRE(vhd.ownsPath(device));
    return call(device);
}

} // namespace

TEST_CASE("An attached virtual disk presents a physical drive path",
          "[diskpart][vhd]")
{
    rpi_test::VhdDevice vhd(64);
    REQUIRE_VHD(vhd);

    // What the rest of the Windows backend is handed. If this is not the shape
    // extractDiskNumber() accepts, none of the cases below mean anything.
    CHECK(vhd.path().startsWith(QStringLiteral("\\\\.\\Physical"),
                                Qt::CaseInsensitive));

    int n = -1;
    REQUIRE(extractDiskNumber(vhd.pathBytes(), n));
    CHECK(n >= 0);
}

TEST_CASE("Rescanning an attached disk succeeds", "[diskpart][vhd]")
{
    rpi_test::VhdDevice vhd(64);
    REQUIRE_VHD(vhd);

    // Non-destructive: it asks Windows to re-read the partition table. Called
    // after every write, successful or not, so a failure here leaves a card
    // that looks missing in Explorer.
    const auto result = onOurDisk(vhd, [](const QByteArray &device) {
        return DiskpartUtil::rescanDisk(device);
    });
    CHECK(result.success);
    CHECK(result.errorMessage.isEmpty());
}

TEST_CASE("Rescanning something that is not a physical drive is a no-op",
          "[diskpart][vhd]")
{
    // Documented as safe on a path that is not a Windows physical drive, and
    // called on paths that come from the user.
    const auto result = DiskpartUtil::rescanDisk(QByteArrayLiteral("/dev/sda"));
    CHECK(result.success);
}

TEST_CASE("Unmounting a disk with no volumes holds nothing open",
          "[diskpart][vhd]")
{
    rpi_test::VhdDevice vhd(64);
    REQUIRE_VHD(vhd);

    // A freshly created VHD has no partition table, so there is nothing to
    // lock. The handles matter: anything adopted here is held open until the
    // caller releases it, and a handle leaked on a disk with no volumes would
    // keep the drive letter of whatever came next.
    DiskpartUtil::LockedVolumes locked;
    const auto result = onOurDisk(vhd, [&locked](const QByteArray &device) {
        return DiskpartUtil::unmountVolumes(device, locked);
    });
    CHECK(result.success);
    CHECK(locked.empty());
}

TEST_CASE("Releasing held volumes twice is safe", "[diskpart][vhd]")
{
    // release() is documented as safe to call more than once, and the write
    // path does exactly that: once when the write finishes and again from the
    // destructor. A double unlock of the same handle would be a use-after-close.
    DiskpartUtil::LockedVolumes locked;
    CHECK(locked.empty());
    CHECK(locked.count() == 0);
    locked.release();
    locked.release();
    CHECK(locked.empty());
}
