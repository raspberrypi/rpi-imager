/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Unit tests for PlatformQuirks functions.
 * Tests pure functions and basic sanity checks.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <QtGlobal>
#include <QString>
#include <QUrl>
#include <QFile>
#include <QTemporaryDir>
#include "platformquirks.h"

#include <cmath>

#include <QProcess>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <thread>

using Catch::Matchers::ContainsSubstring;

// ============================================================================
// Path Transformation Tests (Cross-platform, but macOS has real logic)
// ============================================================================

TEST_CASE("getWriteDevicePath transforms paths correctly", "[platformquirks][path]") {
    SECTION("macOS disk to rdisk transformation") {
#ifdef Q_OS_MACOS
        // macOS should convert /dev/disk to /dev/rdisk for direct I/O
        CHECK(PlatformQuirks::getWriteDevicePath("/dev/disk2") == "/dev/rdisk2");
        CHECK(PlatformQuirks::getWriteDevicePath("/dev/disk0") == "/dev/rdisk0");
        CHECK(PlatformQuirks::getWriteDevicePath("/dev/disk12") == "/dev/rdisk12");
        
        // Should not double-transform already raw paths
        CHECK(PlatformQuirks::getWriteDevicePath("/dev/rdisk2") == "/dev/rdisk2");
        
        // Partition paths should also transform
        CHECK(PlatformQuirks::getWriteDevicePath("/dev/disk2s1") == "/dev/rdisk2s1");
#else
        // Linux and Windows return path unchanged
        CHECK(PlatformQuirks::getWriteDevicePath("/dev/sda") == "/dev/sda");
        CHECK(PlatformQuirks::getWriteDevicePath("\\\\.\\PhysicalDrive0") == "\\\\.\\PhysicalDrive0");
#endif
    }
}

TEST_CASE("getEjectDevicePath transforms paths correctly", "[platformquirks][path]") {
    SECTION("macOS rdisk to disk transformation") {
#ifdef Q_OS_MACOS
        // macOS should convert /dev/rdisk back to /dev/disk for eject operations
        CHECK(PlatformQuirks::getEjectDevicePath("/dev/rdisk2") == "/dev/disk2");
        CHECK(PlatformQuirks::getEjectDevicePath("/dev/rdisk0") == "/dev/disk0");
        CHECK(PlatformQuirks::getEjectDevicePath("/dev/rdisk12") == "/dev/disk12");
        
        // Already block device paths should pass through
        CHECK(PlatformQuirks::getEjectDevicePath("/dev/disk2") == "/dev/disk2");
        
        // Partition paths should also transform
        CHECK(PlatformQuirks::getEjectDevicePath("/dev/rdisk2s1") == "/dev/disk2s1");
#else
        // Linux and Windows return path unchanged
        CHECK(PlatformQuirks::getEjectDevicePath("/dev/sda") == "/dev/sda");
        CHECK(PlatformQuirks::getEjectDevicePath("\\\\.\\PhysicalDrive0") == "\\\\.\\PhysicalDrive0");
#endif
    }
}

TEST_CASE("Path transformation roundtrip", "[platformquirks][path]") {
#ifdef Q_OS_MACOS
    // write -> eject should give back the original block device path
    QString original = "/dev/disk2";
    QString writePath = PlatformQuirks::getWriteDevicePath(original);
    QString ejectPath = PlatformQuirks::getEjectDevicePath(writePath);
    CHECK(ejectPath == original);
    
    // Starting from raw device, eject -> write should give back raw
    QString rawOriginal = "/dev/rdisk5";
    QString ejectFromRaw = PlatformQuirks::getEjectDevicePath(rawOriginal);
    QString writeFromEject = PlatformQuirks::getWriteDevicePath(ejectFromRaw);
    CHECK(writeFromEject == rawOriginal);
#else
    // On non-macOS, both functions are identity transforms
    QString linuxDevice = "/dev/sda";
    CHECK(PlatformQuirks::getWriteDevicePath(linuxDevice) == linuxDevice);
    CHECK(PlatformQuirks::getEjectDevicePath(linuxDevice) == linuxDevice);
#endif
}

// ============================================================================
// Windows-specific tests (enabled via test API)
// ============================================================================

#if defined(Q_OS_WIN) && defined(PLATFORMQUIRKS_ENABLE_TEST_API)

// Declared in platformquirks_windows.cpp when test API is enabled
namespace PlatformQuirks {
namespace TestAPI {
    int parseDeviceNumber(const QString& device);
}
}

TEST_CASE("Windows parseDeviceNumber parses PhysicalDrive paths", "[platformquirks][windows]") {
    using PlatformQuirks::TestAPI::parseDeviceNumber;
    
    SECTION("Valid PhysicalDrive paths with backslashes") {
        CHECK(parseDeviceNumber("\\\\.\\PhysicalDrive0") == 0);
        CHECK(parseDeviceNumber("\\\\.\\PhysicalDrive1") == 1);
        CHECK(parseDeviceNumber("\\\\.\\PhysicalDrive12") == 12);
        CHECK(parseDeviceNumber("\\\\.\\PhysicalDrive99") == 99);
    }
    
    SECTION("Valid PhysicalDrive paths with forward slashes") {
        CHECK(parseDeviceNumber("//./PhysicalDrive0") == 0);
        CHECK(parseDeviceNumber("//./PhysicalDrive5") == 5);
    }
    
    SECTION("Case insensitive parsing") {
        CHECK(parseDeviceNumber("\\\\.\\physicaldrive0") == 0);
        CHECK(parseDeviceNumber("\\\\.\\PHYSICALDRIVE1") == 1);
        CHECK(parseDeviceNumber("\\\\.\\PhYsIcAlDrIvE2") == 2);
    }
    
    SECTION("Invalid paths return -1") {
        CHECK(parseDeviceNumber("") == -1);
        CHECK(parseDeviceNumber("/dev/sda") == -1);
        CHECK(parseDeviceNumber("C:") == -1);
        CHECK(parseDeviceNumber("\\\\.\\HarddiskVolume1") == -1);
        CHECK(parseDeviceNumber("PhysicalDrive0") == -1);  // Missing prefix
    }
}

#endif // Q_OS_WIN && PLATFORMQUIRKS_ENABLE_TEST_API

// ============================================================================
// Basic sanity tests (should not crash, return reasonable values)
// ============================================================================

TEST_CASE("isBeepAvailable returns without crashing", "[platformquirks][sanity]") {
    // Should return a boolean without crashing
    bool available = PlatformQuirks::isBeepAvailable();
    // We can't assert the value since it depends on system configuration,
    // but we can check it's a valid boolean (always true in C++)
    CHECK((available == true || available == false));
    
#ifdef Q_OS_MACOS
    // macOS NSBeep is always available
    CHECK(available == true);
#endif

#ifdef Q_OS_WIN
    // Windows MessageBeep is always available
    CHECK(available == true);
#endif
}

TEST_CASE("hasElevatedPrivileges returns without crashing", "[platformquirks][sanity]") {
    bool elevated = PlatformQuirks::hasElevatedPrivileges();
    CHECK((elevated == true || elevated == false));
    
    // In a normal test environment, we're usually NOT elevated
    // But we can't assert this since CI might run as root
    INFO("Running with elevated privileges: " << elevated);
}

TEST_CASE("prefersReducedMotion returns without crashing", "[platformquirks][sanity]") {
    bool reduced = PlatformQuirks::prefersReducedMotion();
    CHECK((reduced == true || reduced == false));
    INFO("Prefers reduced motion: " << reduced);
}

TEST_CASE("hasNetworkConnectivity returns without crashing", "[platformquirks][sanity]") {
    bool connected = PlatformQuirks::hasNetworkConnectivity();
    CHECK((connected == true || connected == false));
    INFO("Network connectivity: " << connected);
}

TEST_CASE("isNetworkReady returns without crashing", "[platformquirks][sanity]") {
    bool ready = PlatformQuirks::isNetworkReady();
    CHECK((ready == true || ready == false));
    INFO("Network ready: " << ready);
}

// ============================================================================
// DiskResult enum tests
// ============================================================================

TEST_CASE("DiskResult enum has expected values", "[platformquirks][types]") {
    // Verify all enum values are distinct
    CHECK(PlatformQuirks::DiskResult::Success != PlatformQuirks::DiskResult::InvalidDrive);
    CHECK(PlatformQuirks::DiskResult::Success != PlatformQuirks::DiskResult::AccessDenied);
    CHECK(PlatformQuirks::DiskResult::Success != PlatformQuirks::DiskResult::Busy);
    CHECK(PlatformQuirks::DiskResult::Success != PlatformQuirks::DiskResult::Error);
    
    CHECK(PlatformQuirks::DiskResult::InvalidDrive != PlatformQuirks::DiskResult::AccessDenied);
    CHECK(PlatformQuirks::DiskResult::InvalidDrive != PlatformQuirks::DiskResult::Busy);
    CHECK(PlatformQuirks::DiskResult::InvalidDrive != PlatformQuirks::DiskResult::Error);
}

// ============================================================================
// unmountDisk/ejectDisk with invalid paths (safe to test)
// ============================================================================

TEST_CASE("unmountDisk handles invalid device paths", "[platformquirks][disk]") {
    // Empty path
    auto result = PlatformQuirks::unmountDisk("");
    CHECK(result == PlatformQuirks::DiskResult::InvalidDrive);
    
    // Non-existent device
    result = PlatformQuirks::unmountDisk("/dev/nonexistent_device_12345");
    CHECK(result == PlatformQuirks::DiskResult::InvalidDrive);
    
#ifdef Q_OS_WIN
    // Invalid Windows path
    result = PlatformQuirks::unmountDisk("not_a_physical_drive");
    CHECK(result == PlatformQuirks::DiskResult::InvalidDrive);
#endif
}

TEST_CASE("ejectDisk handles invalid device paths", "[platformquirks][disk]") {
    // Empty path
    auto result = PlatformQuirks::ejectDisk("");
    CHECK(result == PlatformQuirks::DiskResult::InvalidDrive);
    
    // Non-existent device
    result = PlatformQuirks::ejectDisk("/dev/nonexistent_device_12345");
    CHECK(result == PlatformQuirks::DiskResult::InvalidDrive);
}

// ============================================================================
// Linux-specific tests
// ============================================================================

#ifdef Q_OS_LINUX

TEST_CASE("findCACertBundle returns valid path or nullptr", "[platformquirks][linux]") {
    const char* caPath = PlatformQuirks::findCACertBundle();
    
    if (caPath != nullptr) {
        // If a path is returned, it should be readable
        INFO("Found CA bundle at: " << caPath);
        // The path should contain common CA bundle path components
        QString path(caPath);
        CHECK((path.contains("ssl") || path.contains("pki") || path.contains("ca")));
    } else {
        // nullptr is valid if no CA bundle is found
        INFO("No CA bundle found (running in minimal environment?)");
    }
}

TEST_CASE("clearAppImageEnvironment doesn't crash", "[platformquirks][linux]") {
    // This is safe to call even outside an AppImage
    // It just unsets LD_LIBRARY_PATH and LD_PRELOAD
    REQUIRE_NOTHROW(PlatformQuirks::clearAppImageEnvironment());
}

TEST_CASE("launchDetached reports exec success and failure", "[platformquirks][linux]") {
    // A real, runnable program (resolved via the internal search path) must
    // report success once execv() has taken: the close-on-exec status pipe
    // yields EOF.
    CHECK(PlatformQuirks::launchDetached("true", QStringList()) == true);

    // A program that cannot be exec'd must report failure rather than masking
    // it behind the double-fork. This is what lets ImageWriter::openUrl fall
    // back to QDesktopServices when xdg-open is missing or unrunnable.
    CHECK(PlatformQuirks::launchDetached("rpi-imager-nonexistent-binary-xyz",
                                         QStringList()) == false);
    CHECK(PlatformQuirks::launchDetached("/nonexistent/path/to/binary",
                                         QStringList()) == false);
}

// Wait for a detached grandchild to produce a file. launchDetached returns as
// soon as exec has taken, not when the program has finished.
static bool waitForFile(const QString& path, int ms = 5000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (QFileInfo::exists(path))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

static bool writeExecutable(const QString& path, const QByteArray& body)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (f.write(body) != body.size())
        return false;
    f.close();
    return f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner);
}

TEST_CASE("A program named without a path is looked for in fixed directories, "
          "not on PATH", "[platformquirks][linux][launch]")
{
    // launchDetached runs xdg-open, xhost and the audio players, and it can be
    // running as root -- Imager elevates itself to write to a disk. Resolving
    // a bare program name against PATH there would mean whoever set PATH
    // before the elevation chooses what runs as root. The code searches
    // /usr/bin, /bin, /usr/sbin and /sbin instead, and consults PATH nowhere.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString marker = tmp.filePath(QStringLiteral("it-ran"));
    const QString planted = tmp.filePath(QStringLiteral("rpi-imager-hijack-probe"));
    REQUIRE(writeExecutable(planted,
                            "#!/bin/sh\ntouch \"" + marker.toUtf8() + "\"\n"));

    // On PATH, and first.
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", tmp.path().toUtf8() + ":" + oldPath);

    const bool launched = PlatformQuirks::launchDetached(
        QStringLiteral("rpi-imager-hijack-probe"), QStringList());

    qputenv("PATH", oldPath);

    // Refused, and -- the part that matters -- never actually run.
    CHECK_FALSE(launched);
    CHECK_FALSE(waitForFile(marker, 1500));
}

TEST_CASE("An absolute path is run exactly as given",
          "[platformquirks][linux][launch]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString marker = tmp.filePath(QStringLiteral("it-ran"));
    const QString script = tmp.filePath(QStringLiteral("runner.sh"));
    REQUIRE(writeExecutable(script,
                            "#!/bin/sh\ntouch \"" + marker.toUtf8() + "\"\n"));

    // Somewhere none of the four search directories would reach. A caller that
    // knows the full path is trusted with it; the search is only for bare
    // names.
    CHECK(PlatformQuirks::launchDetached(script, QStringList()));
    CHECK(waitForFile(marker));
}

TEST_CASE("Arguments reach the program one element each",
          "[platformquirks][linux][launch]")
{
    // What this carries in production is a URL or a file path. There is no
    // shell anywhere in launchDetached -- it is fork, fork, execv -- so an
    // argument with a space in it has to arrive whole. Joined and re-split,
    // a documentation link with a space would open two wrong pages, and a
    // path with one would name a file that does not exist.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString out = tmp.filePath(QStringLiteral("argv.txt"));
    const QString script = tmp.filePath(QStringLiteral("dump.sh"));
    REQUIRE(writeExecutable(script,
                            "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$1\"\n"));

    const QStringList args{
        out,
        QStringLiteral("https://example.invalid/a b?x=1&y=2"),
        QStringLiteral("two  spaces"),
        QStringLiteral("semi;colon"),
    };
    REQUIRE(PlatformQuirks::launchDetached(script, args));
    REQUIRE(waitForFile(out));

    QFile f(out);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QStringList got = QString::fromUtf8(f.readAll())
                                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    CHECK(got == args);
}

TEST_CASE("registerUriScheme writes the rpi-imager:// desktop entry", "[platformquirks][linux]") {
    // Redirect the XDG data/config dirs to a temp location so the test neither
    // pollutes nor depends on the real user environment (any update-desktop-
    // database / xdg-mime side effects land in the temp dirs).
    QTemporaryDir dataHome;
    QTemporaryDir configHome;
    REQUIRE(dataHome.isValid());
    REQUIRE(configHome.isValid());

    const QByteArray prevData = qgetenv("XDG_DATA_HOME");
    const QByteArray prevConfig = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());

    CHECK(PlatformQuirks::registerUriScheme() == true);

    const QString desktopPath = dataHome.path()
        + "/applications/com.raspberrypi.rpi-imager-uri-handler.desktop";
    QFile f(desktopPath);
    REQUIRE(f.exists());
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray firstWrite = f.readAll();
    f.close();

    const QString contents = QString::fromUtf8(firstWrite);
    // The entry must claim the scheme and point Exec at this executable with %u.
    CHECK(contents.contains("MimeType=x-scheme-handler/rpi-imager;"));
    CHECK(contents.contains("Exec="));
    CHECK(contents.contains("%u"));
    CHECK(contents.contains("NoDisplay=true"));

    // Idempotent: a second call leaves the entry byte-for-byte identical.
    CHECK(PlatformQuirks::registerUriScheme() == true);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray secondWrite = f.readAll();
    f.close();
    CHECK(firstWrite == secondWrite);

    // Restore the environment for subsequent tests.
    if (prevData.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", prevData);
    if (prevConfig.isNull()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", prevConfig);
}

TEST_CASE("getBundlePath resolves executable outside AppImage", "[platformquirks][linux]") {
    // When not running from an AppImage, getBundlePath falls back to /proc/self/exe
    const char* appImage = getenv("APPIMAGE");
    if (appImage == nullptr) {
        const char* path = PlatformQuirks::getBundlePath();
        // Should resolve to the test binary via /proc/self/exe
        CHECK(path != nullptr);
        CHECK(access(path, F_OK) == 0);
        // isElevatableBundle should be true (binary exists), even though
        // tryElevate will bail out if no polkit policy is installed
        CHECK(PlatformQuirks::isElevatableBundle() == true);
    }
}

#endif // Q_OS_LINUX

// ============================================================================
// macOS-specific tests
// ============================================================================

#ifdef Q_OS_MACOS

TEST_CASE("macOS path edge cases", "[platformquirks][macos][path]") {
    // Empty string
    CHECK(PlatformQuirks::getWriteDevicePath("") == "");
    CHECK(PlatformQuirks::getEjectDevicePath("") == "");
    
    // Path without /dev/disk prefix (should pass through unchanged)
    CHECK(PlatformQuirks::getWriteDevicePath("/some/other/path") == "/some/other/path");
    CHECK(PlatformQuirks::getEjectDevicePath("/some/other/path") == "/some/other/path");
    
    // Partial matches shouldn't transform
    // Note: QString::replace will still match "disk" inside other words
    // This tests current behavior, not necessarily ideal behavior
    QString testPath = "/dev/diskette";
    QString writePath = PlatformQuirks::getWriteDevicePath(testPath);
    // Current implementation will transform this - documenting actual behavior
    INFO("diskette path becomes: " << writePath.toStdString());
}

TEST_CASE("macOS isScrollInverted passes through Qt flag", "[platformquirks][macos]") {
    // On macOS, the function just returns the Qt flag as-is
    CHECK(PlatformQuirks::isScrollInverted(true) == true);
    CHECK(PlatformQuirks::isScrollInverted(false) == false);
}

TEST_CASE("registerUriScheme is safe to call on macOS", "[platformquirks][macos]") {
    // A bare test binary has no bundle identifier, so this is a no-op that
    // returns false without touching Launch Services. The assertion is that
    // the PAL entry point is wired up and side-effect-free in that case.
    CHECK(PlatformQuirks::registerUriScheme() == false);
}

#endif // Q_OS_MACOS

// ============================================================================
// Windows-specific URL scheme tests
// ============================================================================

#ifdef Q_OS_WIN

TEST_CASE("registerUriScheme is a runtime no-op on Windows", "[platformquirks][windows]") {
    // The rpi-imager:// association is written to the registry by the installer,
    // so the runtime call has nothing to do and reports success.
    CHECK(PlatformQuirks::registerUriScheme() == true);
}

TEST_CASE("openUrlExternally declines native launch on Windows", "[platformquirks][windows]") {
    // By design the PAL does not open URLs itself on Windows (avoids routing the
    // URL through a shell); it reports false so the caller uses QDesktopServices.
    CHECK(PlatformQuirks::openUrlExternally(QUrl("https://example.com/")) == false);
}

#endif // Q_OS_WIN

// ============================================================================
// Network monitoring lifecycle tests
// ============================================================================

TEST_CASE("Network monitoring can be started and stopped", "[platformquirks][network]") {
    bool callbackInvoked = false;
    
    // Start monitoring with a callback
    PlatformQuirks::startNetworkMonitoring([&callbackInvoked](bool available) {
        callbackInvoked = true;
        INFO("Network status callback: " << available);
    });
    
    // Stop monitoring
    PlatformQuirks::stopNetworkMonitoring();
    
    // Should not crash when stopping again
    REQUIRE_NOTHROW(PlatformQuirks::stopNetworkMonitoring());
}

TEST_CASE("Network monitoring handles null callback gracefully", "[platformquirks][network]") {
    // This shouldn't crash
    PlatformQuirks::startNetworkMonitoring(nullptr);
    PlatformQuirks::stopNetworkMonitoring();
}

// How many descriptors and threads this process is holding. The monitor takes
// one netlink socket, one eventfd and one thread each time it starts.
//
// Both counts come from procfs, which is a Linux thing: on macOS the counter
// below reads an empty directory and answers zero for everything, so the two
// cases that use it were failing on their own precondition rather than on the
// leak they are about.
static bool haveProcfsCounts()
{
    return QDir(QStringLiteral("/proc/self/fd")).exists();
}

static int countEntries(const char* dir)
{
    return QDir(QString::fromLatin1(dir)).entryList(QDir::Files | QDir::Dirs |
                                                    QDir::NoDotAndDotDot |
                                                    QDir::System).size();
}

// Both counts are process-wide, and the process is more than this test: Qt
// starts and retires threads of its own, and a descriptor opened elsewhere
// lands in the same directory. Sampling once, immediately after a stop,
// makes the case fail on somebody else's timing -- it did, in a loaded -j4
// run. Wait for the count to come back instead, and only then insist.
static bool settlesTo(const char* dir, int target, int milliseconds = 5000)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(milliseconds);
    for (;;) {
        if (countEntries(dir) <= target)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

TEST_CASE("Watching for the network back does not accumulate",
          "[platformquirks][network]") {
    // isOnline() calls startNetworkMonitoring on every poll while there is no
    // network and no OS list -- once a second, for as long as the machine
    // stays offline. Nothing stops the previous watcher first; start is
    // expected to do that itself.
    //
    // If it does not, the application leaks a netlink socket, an eventfd and
    // a joinable thread every second that the offline screen is up. What the
    // user then sees is not a network problem: it is the write failing to
    // open the storage device, hours later, because the process has run out
    // of descriptors.
    if (!haveProcfsCounts())
        SKIP("counting descriptors and threads needs procfs");

    const int fdsBefore = countEntries("/proc/self/fd");
    const int threadsBefore = countEntries("/proc/self/task");
    REQUIRE(fdsBefore > 0);

    for (int i = 0; i < 8; i++)
        PlatformQuirks::startNetworkMonitoring([](bool) {});

    // One watcher's worth, however many times it was asked for.
    CHECK(countEntries("/proc/self/fd") - fdsBefore <= 2);
    CHECK(countEntries("/proc/self/task") - threadsBefore <= 1);

    PlatformQuirks::stopNetworkMonitoring();

    // And stopping gives all of it back.
    //
    // Note what the thread count can and cannot see: it catches a watcher
    // left running, but not a thread that has exited without being joined --
    // that one is gone from /proc/self/task while still holding its stack.
    // Dropping the pthread_join fails neither assertion here.
    CHECK(settlesTo("/proc/self/fd", fdsBefore));
    CHECK(settlesTo("/proc/self/task", threadsBefore));
}

TEST_CASE("Start and stop in a loop leaves nothing behind",
          "[platformquirks][network]") {
    if (!haveProcfsCounts())
        SKIP("counting descriptors and threads needs procfs");

    const int fdsBefore = countEntries("/proc/self/fd");
    const int threadsBefore = countEntries("/proc/self/task");

    for (int i = 0; i < 5; i++) {
        PlatformQuirks::startNetworkMonitoring([](bool) {});
        PlatformQuirks::stopNetworkMonitoring();
    }

    CHECK(settlesTo("/proc/self/fd", fdsBefore));
    CHECK(settlesTo("/proc/self/task", threadsBefore));

    // A stop with nothing running is not a way to lose a descriptor either.
    PlatformQuirks::stopNetworkMonitoring();
    CHECK(settlesTo("/proc/self/fd", fdsBefore));
}

#ifdef Q_OS_LINUX
// ============================================================================
// unmountDisk: the success path
//
// The rejection cases above stop nonsense reaching the kernel. This covers
// the case the function actually exists for: a card the user has mounted,
// about to be written to. If it silently fails to unmount, the write goes to
// a device the kernel still has a dirty page cache for, and the filesystem
// is corrupted -- the write "succeeds" and the card is unreadable.
//
// Uses a loop device so nothing outside this test is touched. Skipped when
// the suite cannot get the privileges to set one up.
// ============================================================================

#include "faulty_block_device.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QUuid>

#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <pwd.h>
#include <unistd.h>

namespace {

// unmountDisk() calls umount(2) in-process, which needs the *test binary*
// to be root -- having passwordless sudo for fixture setup is not enough.
// So these skip unless the suite is run as root (`sudo ctest`), rather than
// reporting a failure for a privilege the runner was never given.
bool runningAsRoot() { return ::geteuid() == 0; }

// Is `path` (or anything beneath it) currently a mount source or target?
bool appearsInMounts(const QString &needle)
{
    QFile mounts(QStringLiteral("/proc/mounts"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    return QString::fromUtf8(mounts.readAll()).contains(needle);
}

// A loop device carrying a single FAT partition, mounted. Tears itself down
// on destruction whether or not the test unmounted it.
class MountedLoopDisk
{
public:
    MountedLoopDisk()
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        _dir = QDir::temp().filePath(QStringLiteral("rpi-imager-um-%1").arg(id));
        QDir().mkpath(_dir);
        _mountPoint = QDir(_dir).filePath(QStringLiteral("mnt"));
        QDir().mkpath(_mountPoint);

        const QString backing = QDir(_dir).filePath(QStringLiteral("disk.img"));

        // 64 MiB: one MBR partition starting at LBA 2048, FAT32-typed.
        QByteArray image(64 * 1024 * 1024, '\0');
        auto *mbr = reinterpret_cast<uint8_t *>(image.data());
        mbr[0x1BE + 0x04] = 0x0C;                    // type: FAT32 LBA
        mbr[0x1BE + 0x08] = 0x00;                    // first LBA = 2048
        mbr[0x1BE + 0x09] = 0x08;
        const uint32_t sectors = (64u * 1024 * 1024) / 512 - 2048;
        std::memcpy(mbr + 0x1BE + 0x0C, &sectors, 4);
        mbr[0x1FE] = 0x55;
        mbr[0x1FF] = 0xAA;

        QFile f(backing);
        if (!f.open(QIODevice::WriteOnly))
            return;
        f.write(image);
        f.close();

        // -P so the kernel exposes the partition as <loop>p1.
        QByteArray out;
        if (!rpi_imager::testing::runPrivileged(QStringLiteral("losetup"),
                                     {QStringLiteral("-fP"), QStringLiteral("--show"), backing},
                                     &out))
            return;
        _loop = QString::fromUtf8(out).trimmed();
        if (_loop.isEmpty())
            return;

        _partition = _loop + QStringLiteral("p1");
        if (!rpi_imager::testing::runPrivileged(QStringLiteral("mkfs.vfat"),
                                     {QStringLiteral("-F"), QStringLiteral("32"), _partition}))
            return;
        if (!rpi_imager::testing::runPrivileged(QStringLiteral("mount"), {_partition, _mountPoint}))
            return;

        _ready = true;
    }

    ~MountedLoopDisk()
    {
        if (!_mountPoint.isEmpty())
            rpi_imager::testing::runPrivileged(QStringLiteral("umount"), {_mountPoint});
        if (!_loop.isEmpty())
            rpi_imager::testing::runPrivileged(QStringLiteral("losetup"), {QStringLiteral("-d"), _loop});
        QDir(_dir).removeRecursively();
    }

    MountedLoopDisk(const MountedLoopDisk &) = delete;
    MountedLoopDisk &operator=(const MountedLoopDisk &) = delete;

    bool ready() const { return _ready; }
    QString disk() const { return _loop; }
    QString partition() const { return _partition; }
    QString mountPoint() const { return _mountPoint; }

private:
    QString _dir, _mountPoint, _loop, _partition;
    bool _ready = false;
};

} // namespace

// The only two cases in the suite that a non-root run genuinely loses.
// Everything else that needs privilege adapts instead -- chowning the loop
// device it made, or reaching for sudo -- so these are the exception rather
// than the rule.
//
// They are safe to run as root: MountedLoopDisk creates its own backing
// file, attaches its own loop device and mounts it inside a temporary
// directory, then detaches on the way out. Nothing pre-existing is touched.
//
//     sudo ctest -R privileged        (or)
//     sudo ./platformquirks_test "[privileged]"
//
// Doing so takes platformquirks_linux.cpp from 282 to 315 branches taken --
// the real umount(2) path, which cannot be reached any other way.
TEST_CASE("unmountDisk unmounts a mounted partition of the target disk",
          "[platformquirks][disk][privileged]") {
    if (!runningAsRoot())
        SKIP("unmountDisk calls umount(2) in-process; run the suite as root");

    MountedLoopDisk disk;
    if (!disk.ready())
        SKIP("could not set up a mounted loop device");

    // Precondition: the partition really is mounted.
    REQUIRE(appearsInMounts(disk.partition()));

    // The caller passes the whole disk, as the write path does -- the
    // mounted thing is the partition inside it.
    const auto result = PlatformQuirks::unmountDisk(disk.disk());
    CHECK(result == PlatformQuirks::DiskResult::Success);

    CHECK_FALSE(appearsInMounts(disk.partition()));
}

TEST_CASE("unmountDisk succeeds on a disk that is not mounted",
          "[platformquirks][disk][privileged]") {
    // Nothing to unmount is not a failure: the write can proceed. Reporting
    // an error here would block writing to a perfectly good blank card.
    if (!runningAsRoot())
        SKIP("unmountDisk calls umount(2) in-process; run the suite as root");

    MountedLoopDisk disk;
    if (!disk.ready())
        SKIP("could not set up a mounted loop device");

    REQUIRE(PlatformQuirks::unmountDisk(disk.disk()) == PlatformQuirks::DiskResult::Success);
    CHECK_FALSE(appearsInMounts(disk.partition()));

    // Second call has nothing left to do.
    CHECK(PlatformQuirks::unmountDisk(disk.disk()) == PlatformQuirks::DiskResult::Success);
}

TEST_CASE("unmountDisk rejects a directory", "[platformquirks][disk]") {
    // Guards against a path that exists but is not a device: unmounting
    // whatever happens to be under it would be destructive.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    CHECK(PlatformQuirks::unmountDisk(dir.path()) == PlatformQuirks::DiskResult::InvalidDrive);
}
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
// ============================================================================
// Display scaling and environment helpers
// ============================================================================
//
// Small exported functions, each with a real consequence: the font scale the
// window is laid out at, and the environment handed to external tools.

#include <QProcessEnvironment>

TEST_CASE("An explicit QT_SCALE_FACTOR overrides desktop text scaling",
          "[platformquirks][scaling]") {
    // Somebody who has set QT_SCALE_FACTOR has already said what they want.
    // Multiplying the desktop's text-scaling factor on top of it would
    // double-apply and leave the window unusable at high settings.
    const QByteArray saved = qgetenv("QT_SCALE_FACTOR");
    qputenv("QT_SCALE_FACTOR", "2");

    CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);

    if (saved.isEmpty())
        qunsetenv("QT_SCALE_FACTOR");
    else
        qputenv("QT_SCALE_FACTOR", saved);
}

TEST_CASE("Text scaling returns a usable factor without one",
          "[platformquirks][scaling]") {
    // Whatever the desktop reports, the result has to be a sane multiplier:
    // zero or a negative would collapse or invert every measurement derived
    // from it.
    const QByteArray saved = qgetenv("QT_SCALE_FACTOR");
    qunsetenv("QT_SCALE_FACTOR");

    const qreal f = PlatformQuirks::detectTextScaleFactor();
    INFO("factor: " << f);
    CHECK(f > 0.0);
    CHECK(f < 10.0);

    if (!saved.isEmpty())
        qputenv("QT_SCALE_FACTOR", saved);
}

// ══════════════════════════════════════════════════════════════
// Following the desktop's text size.
//
// Somebody who has turned their system text size up has done so because
// they need it. detectTextScaleFactor asks the desktop three ways in turn
// and the answer sizes the whole Imager window; returning 1.0 when it should
// not leaves that person with a window they cannot read, and there is
// nothing in Imager's own settings to correct it with.
//
// Only the QT_SCALE_FACTOR short-circuit was covered. The three strategies
// shell out to `gsettings` by name, so a fake one earlier on PATH is enough
// to drive each of them.
// ══════════════════════════════════════════════════════════════

namespace {

// Puts a controlled PATH and a clean environment around one case, and gives
// it back afterwards. Everything here is process-wide, and Catch2 runs the
// cases in a random order.
class ScalingEnvironment
{
public:
    ScalingEnvironment()
        : _path(qgetenv("PATH")),
          _qtScaleFactor(qgetenv("QT_SCALE_FACTOR")),
          _gdkDpiScale(qgetenv("GDK_DPI_SCALE"))
    {
        REQUIRE(_dir.isValid());
        qunsetenv("QT_SCALE_FACTOR");
        qunsetenv("GDK_DPI_SCALE");
        // Only what this case plants is findable, so the machine's own
        // gsettings cannot answer for the fixture.
        qputenv("PATH", _dir.path().toUtf8());
    }

    ~ScalingEnvironment()
    {
        qputenv("PATH", _path);
        restore("QT_SCALE_FACTOR", _qtScaleFactor);
        restore("GDK_DPI_SCALE", _gdkDpiScale);
    }

    // A gsettings that answers with the two values this case wants. An empty
    // string means the key reads back as nothing, which is what an unset key
    // or a missing schema looks like.
    void plantGsettings(const QString& textScalingFactor, const QString& fontName)
    {
        const QByteArray script =
            "#!/bin/sh\n"
            "case \"$3\" in\n"
            "  text-scaling-factor) printf '%s\\n' \"" + textScalingFactor.toUtf8() + "\" ;;\n"
            "  font-name) printf '%s\\n' \"" + fontName.toUtf8() + "\" ;;\n"
            "esac\n";
        const QString path = _dir.filePath(QStringLiteral("gsettings"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(f.write(script) == script.size());
        f.close();
        REQUIRE(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner));
    }

private:
    static void restore(const char* name, const QByteArray& value)
    {
        if (value.isEmpty())
            qunsetenv(name);
        else
            qputenv(name, value);
    }

    QTemporaryDir _dir;
    QByteArray _path;
    QByteArray _qtScaleFactor;
    QByteArray _gdkDpiScale;
};

} // namespace

TEST_CASE("A text-scaling factor set in the desktop is followed",
          "[platformquirks][scaling]")
{
    ScalingEnvironment env;

    SECTION("a plain enlargement")
    {
        env.plantGsettings(QStringLiteral("1.5"), QString());
        CHECK(std::abs(PlatformQuirks::detectTextScaleFactor() - 1.5) < 1e-9);
    }

    SECTION("and a reduction, which is just as deliberate")
    {
        env.plantGsettings(QStringLiteral("0.75"), QString());
        CHECK(std::abs(PlatformQuirks::detectTextScaleFactor() - 0.75) < 1e-9);
    }
}

TEST_CASE("An implausible scaling factor is not applied",
          "[platformquirks][scaling]")
{
    // The value comes back from a subprocess as text. Something far outside
    // the range is a misread, not a request -- and applying it would leave a
    // window either unreadable or off the screen entirely.
    ScalingEnvironment env;

    SECTION("far too large")
    {
        env.plantGsettings(QStringLiteral("40"), QString());
        CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);
    }

    SECTION("far too small")
    {
        env.plantGsettings(QStringLiteral("0.05"), QString());
        CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);
    }

    SECTION("not a number at all")
    {
        env.plantGsettings(QStringLiteral("no schema for this key"), QString());
        CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);
    }
}

TEST_CASE("A factor that rounds to no change is treated as none",
          "[platformquirks][scaling]")
{
    // Within five per cent of 1.0 is left alone rather than resizing the
    // whole window by an amount nobody asked for and nobody would notice
    // except as blurry text.
    ScalingEnvironment env;
    env.plantGsettings(QStringLiteral("1.02"), QString());
    CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);
}

TEST_CASE("A larger desktop font is followed when no scaling factor is set",
          "[platformquirks][scaling]")
{
    // Raspberry Pi OS's own accessibility setting changes the font size
    // rather than the scaling factor, so this is the strategy that fires on
    // the hardware Imager most often runs on.
    ScalingEnvironment env;

    SECTION("the size is taken from the end of the font name")
    {
        // gsettings quotes its answers; the quotes have to come off or the
        // size never parses and a Pi user's larger text is ignored.
        env.plantGsettings(QStringLiteral("1.0"), QStringLiteral("'PibotoLt 14'"));
        CHECK(std::abs(PlatformQuirks::detectTextScaleFactor() - 1.4) < 1e-9);
    }

    SECTION("a font family with a space in it still parses")
    {
        env.plantGsettings(QStringLiteral("1.0"), QStringLiteral("'DejaVu Sans 15'"));
        CHECK(std::abs(PlatformQuirks::detectTextScaleFactor() - 1.5) < 1e-9);
    }

    SECTION("the default size means no scaling")
    {
        env.plantGsettings(QStringLiteral("1.0"), QStringLiteral("'PibotoLt 10'"));
        CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);
    }

    SECTION("a font name with no size is not read as one")
    {
        env.plantGsettings(QStringLiteral("1.0"), QStringLiteral("'PibotoLt'"));
        CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);
    }
}

TEST_CASE("GDK_DPI_SCALE is the last thing asked", "[platformquirks][scaling]")
{
    // No gsettings on the machine at all -- a minimal desktop, or a
    // non-GNOME one. The environment variable is what is left.
    ScalingEnvironment env;

    SECTION("and it is followed")
    {
        qputenv("GDK_DPI_SCALE", "1.25");
        CHECK(std::abs(PlatformQuirks::detectTextScaleFactor() - 1.25) < 1e-9);
    }

    SECTION("but only within the same range as the others")
    {
        qputenv("GDK_DPI_SCALE", "12");
        CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);
    }

    SECTION("and gsettings is preferred over it where both are set")
    {
        env.plantGsettings(QStringLiteral("1.5"), QString());
        qputenv("GDK_DPI_SCALE", "2.0");
        CHECK(std::abs(PlatformQuirks::detectTextScaleFactor() - 1.5) < 1e-9);
    }
}

TEST_CASE("A desktop that says nothing leaves the window as it is",
          "[platformquirks][scaling]")
{
    ScalingEnvironment env;
    CHECK(PlatformQuirks::detectTextScaleFactor() == 1.0);
}

TEST_CASE("The font DPI correction is the ratio it claims to be",
          "[platformquirks][scaling]") {
    // 72/96: points to pixels. Wrong here and every font in the window is
    // the wrong size.
    const qreal c = PlatformQuirks::fontDpiCorrection();
    INFO("correction: " << c);
    CHECK(c > 0.0);
    CHECK(std::abs(c - (72.0 / 96.0)) < 1e-9);
}

TEST_CASE("Clearing the AppImage environment removes both loader variables",
          "[platformquirks][env]") {
    // An AppImage points these at its bundled libraries. Leaving them set
    // for a forked tool makes it load our Qt instead of the system's, which
    // surfaces as PAM modules refusing to open a session or KDE tools
    // failing on a Qt version mismatch -- neither of which looks like an
    // environment problem from the outside.
    qputenv("LD_LIBRARY_PATH", "/opt/appimage/lib");
    qputenv("LD_PRELOAD", "/opt/appimage/lib/libthing.so");
    REQUIRE_FALSE(qgetenv("LD_LIBRARY_PATH").isEmpty());

    PlatformQuirks::clearAppImageEnvironment();

    CHECK(qgetenv("LD_LIBRARY_PATH").isEmpty());
    CHECK(qgetenv("LD_PRELOAD").isEmpty());
}

TEST_CASE("Clearing an already-clean environment is harmless",
          "[platformquirks][env]") {
    qunsetenv("LD_LIBRARY_PATH");
    qunsetenv("LD_PRELOAD");
    CHECK_NOTHROW(PlatformQuirks::clearAppImageEnvironment());
    CHECK(qgetenv("LD_LIBRARY_PATH").isEmpty());
}

TEST_CASE("Scroll direction follows the flag Qt reports",
          "[platformquirks][input]") {
    // On Linux the platform has no opinion of its own, so Qt's flag is
    // passed through. Inverting it here would reverse scrolling for
    // everybody who has natural scrolling switched on.
    CHECK(PlatformQuirks::isScrollInverted(true));
    CHECK_FALSE(PlatformQuirks::isScrollInverted(false));
}
#endif // Q_OS_LINUX

// ============================================================================
// The polkit policy an AppImage installs for itself
//
// An AppImage cannot write to a block device unelevated, so it installs a
// polkit action granting pkexec the right to run it as root. Two pure
// functions decide that file: one escapes the text that goes into it, the
// other names it. Both are worth pinning -- the document grants root, and its
// content includes a path the user chose by where they saved the AppImage.
// ============================================================================

#if defined(Q_OS_LINUX) && defined(PLATFORMQUIRKS_ENABLE_TEST_API)

#include <QXmlStreamReader>

namespace PlatformQuirks {
namespace TestAPI {
    QString xmlEscape(const QString& input);
    bool generatePolkitPolicyFilename(const char* appImagePath, char* buffer, size_t bufferSize);
}
}

namespace {

// What an XML parser makes of the escaped text, which is the only opinion
// that matters -- the policy is read by polkit's parser, not by us.
QString parsedBack(const QString& escaped)
{
    const QString doc = QStringLiteral("<r>") + escaped + QStringLiteral("</r>");
    QXmlStreamReader xml(doc);
    QString text;
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::Characters)
            text += xml.text().toString();
    }
    REQUIRE_FALSE(xml.hasError());
    return text;
}

} // namespace

TEST_CASE("XML escaping survives a round trip through a parser",
          "[platformquirks][polkit]")
{
    // Every one of these appears in real filesystem paths.
    const QStringList inputs = {
        QStringLiteral("/home/user/Applications/rpi-imager.AppImage"),
        QStringLiteral("/home/user/R&D/rpi-imager.AppImage"),
        QStringLiteral("/home/user/\"quoted\"/rpi-imager.AppImage"),
        QStringLiteral("/home/user/it's mine/rpi-imager.AppImage"),
        QStringLiteral("/home/user/<angle>/rpi-imager.AppImage"),
        QStringLiteral("/tmp/a&b<c>d\"e'f"),
        QStringLiteral(""),
    };

    for (const QString& in : inputs) {
        const QString escaped = PlatformQuirks::TestAPI::xmlEscape(in);
        INFO("input:   " << in.toStdString());
        INFO("escaped: " << escaped.toStdString());
        CHECK(parsedBack(escaped) == in);
    }
}

TEST_CASE("XML escaping leaves no raw markup behind", "[platformquirks][polkit]")
{
    // The specific failure that matters: a path that closes the element it is
    // sitting in and opens one of its own would rewrite the policy.
    const QString hostile =
        QStringLiteral("/tmp/x</annotate><annotate key=\"org.freedesktop.policykit.exec.path\">/bin/sh");
    const QString escaped = PlatformQuirks::TestAPI::xmlEscape(hostile);

    INFO("escaped: " << escaped.toStdString());
    CHECK_FALSE(escaped.contains(QLatin1Char('<')));
    CHECK_FALSE(escaped.contains(QLatin1Char('>')));
    CHECK_FALSE(escaped.contains(QLatin1Char('"')));
    CHECK(parsedBack(escaped) == hostile);
}

TEST_CASE("XML escaping keeps the whitespace XML allows", "[platformquirks][polkit]")
{
    // Tab, newline and carriage return are legal content; escaping them would
    // be harmless but wrong, and the other control characters are not legal
    // and have to go.
    CHECK(PlatformQuirks::TestAPI::xmlEscape(QStringLiteral("a\tb\nc\rd"))
          == QStringLiteral("a\tb\nc\rd"));

    const QString withNul = QStringLiteral("a") + QChar(0x01) + QStringLiteral("b");
    const QString escaped = PlatformQuirks::TestAPI::xmlEscape(withNul);
    INFO("escaped: " << escaped.toStdString());
    CHECK(escaped == QStringLiteral("a&#x1;b"));
}

TEST_CASE("A policy filename is stable, unique and stays in its directory",
          "[platformquirks][polkit]")
{
    char a[128] = {0}, b[128] = {0}, again[128] = {0};

    REQUIRE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/home/user/Apps/rpi-imager.AppImage", a, sizeof(a)));
    REQUIRE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/opt/rpi-imager.AppImage", b, sizeof(b)));
    REQUIRE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/home/user/Apps/rpi-imager.AppImage", again, sizeof(again)));

    const QString nameA = QString::fromLatin1(a);
    INFO("A: " << nameA.toStdString() << "  B: " << b);

    // The same AppImage keeps the same policy, so a second run does not
    // litter the actions directory with a new file each time.
    CHECK(nameA == QString::fromLatin1(again));

    // Two AppImages in different places do not share one.
    CHECK(nameA != QString::fromLatin1(b));

    CHECK(nameA.startsWith(QStringLiteral("com.raspberrypi.rpi-imager.appimage-")));
    CHECK(nameA.endsWith(QStringLiteral(".policy")));

    // It is joined onto a directory, so anything that looks like a path
    // would write somewhere nobody intended.
    CHECK_FALSE(nameA.contains(QLatin1Char('/')));
    CHECK_FALSE(nameA.contains(QStringLiteral("..")));
}

TEST_CASE("A policy filename refuses what it cannot write", "[platformquirks][polkit]")
{
    char buf[128] = {0};
    CHECK_FALSE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        nullptr, buf, sizeof(buf)));
    CHECK_FALSE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/opt/x.AppImage", nullptr, sizeof(buf)));

    // Too small to hold the name: better to say so than to write a truncated
    // filename that silently becomes a different policy.
    char tiny[32] = {0};
    CHECK_FALSE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/opt/x.AppImage", tiny, sizeof(tiny)));
}

#endif // Q_OS_LINUX && PLATFORMQUIRKS_ENABLE_TEST_API

#ifdef Q_OS_LINUX
// ============================================================================
// The monitor with something to notice
//
// The three cases above start the watcher and stop it again, which is worth
// knowing but leaves the thread with nothing to read: no link changes state
// inside the moment they run, so the loop polls once and exits. Everything
// past the poll -- reading the netlink message, walking the headers, deciding
// a link went up, dropping the cached answer and telling the caller -- had
// never run.
//
// It is the part that matters. The wizard asks whether there is a network to
// decide whether the device list can be fetched at all, and caches the
// answer. A user who plugs in a cable after starting Imager depends entirely
// on this thread noticing: without it they stay on the offline screen, with a
// working connection, and nothing tells them to restart the application.
//
// So a link is actually changed. A dummy interface, created for the test and
// removed after: it carries no traffic, gets no address and no route, and
// touches nothing of the host's. Named distinctively so a leftover is
// obviously the test's own.
// ============================================================================

namespace {

bool runAsRoot(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("sudo"),
            QStringList{QStringLiteral("-n")} + args);
    if (!p.waitForFinished(15000))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

bool haveRoot()
{
    return runAsRoot({QStringLiteral("true")});
}

// A dummy interface that removes itself.
class DummyLink
{
public:
    explicit DummyLink(const QString &name) : _name(name)
    {
        // Any leftover from a previous run, so the add below does not fail on
        // a name that is already taken.
        runAsRoot({QStringLiteral("ip"), QStringLiteral("link"),
                   QStringLiteral("del"), _name});
        _created = runAsRoot({QStringLiteral("ip"), QStringLiteral("link"),
                              QStringLiteral("add"), _name,
                              QStringLiteral("type"), QStringLiteral("dummy")});
    }

    ~DummyLink()
    {
        if (_created)
            runAsRoot({QStringLiteral("ip"), QStringLiteral("link"),
                       QStringLiteral("del"), _name});
    }

    DummyLink(const DummyLink &) = delete;
    DummyLink &operator=(const DummyLink &) = delete;

    bool created() const { return _created; }

    bool bringUp() const
    {
        return runAsRoot({QStringLiteral("ip"), QStringLiteral("link"),
                          QStringLiteral("set"), _name, QStringLiteral("up")});
    }

    bool takeDown() const
    {
        return runAsRoot({QStringLiteral("ip"), QStringLiteral("link"),
                          QStringLiteral("set"), _name, QStringLiteral("down")});
    }

private:
    QString _name;
    bool _created = false;
};

} // namespace

TEST_CASE("A link changing state reaches the callback", "[platformquirks][network][root]")
{
    if (!haveRoot())
        SKIP("passwordless sudo is not available, so no interface can be "
             "brought up for the monitor to notice");

    DummyLink link(QStringLiteral("rpiimgtest0"));
    if (!link.created())
        SKIP("no dummy interface could be created on this host");

    std::atomic<int> calls{0};
    std::atomic<bool> sawAnswer{false};
    PlatformQuirks::startNetworkMonitoring([&](bool available) {
        sawAnswer.store(available || true);
        calls.fetch_add(1);
    });

    // Up, then down: two changes, so a run that happened to miss the first
    // still has one to catch, and the loop goes round more than once.
    CHECK(link.bringUp());
    CHECK(link.takeDown());

    // Generous, because the callback asks the system whether there is a
    // network before answering and that can take a moment.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    PlatformQuirks::stopNetworkMonitoring();

    CHECK(calls.load() > 0);
    CHECK(sawAnswer.load());
}

TEST_CASE("Stopping the monitor stops the callbacks", "[platformquirks][network][root]")
{
    // The other half. The watcher outlives the screen that asked for it, and
    // a callback arriving after the caller has gone is a use-after-free
    // rather than a missed notification.
    if (!haveRoot())
        SKIP("passwordless sudo is not available");

    DummyLink link(QStringLiteral("rpiimgtest1"));
    if (!link.created())
        SKIP("no dummy interface could be created on this host");

    std::atomic<int> calls{0};
    PlatformQuirks::startNetworkMonitoring([&](bool) { calls.fetch_add(1); });

    CHECK(link.bringUp());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(calls.load() > 0);

    PlatformQuirks::stopNetworkMonitoring();
    const int after = calls.load();

    CHECK(link.takeDown());
    std::this_thread::sleep_for(std::chrono::seconds(2));

    CHECK(calls.load() == after);
}
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
// ══════════════════════════════════════════════════════════════
// The polkit policy: its content and its name
//
// An AppImage cannot write to a disk without a polkit policy granting pkexec
// the right to run it as root. Imager installs one, naming the binary it
// authorises. Two internal functions decide what goes in it and what it is
// called, and the file carries a comment saying they are "pure and worth
// pinning" -- with a test API written for the purpose that nothing used.
//
// Both risks named in that comment are real. The document is XML and the
// path goes into it verbatim, so a path containing the right characters
// either breaks the document or -- worse -- closes the annotation and opens
// elements of its own inside a file that grants root. And the filename is
// derived from the path, so a mistake there either collides with another
// AppImage's policy or escapes the actions directory.
// ══════════════════════════════════════════════════════════════

namespace PlatformQuirks::TestAPI {
QString xmlEscape(const QString& input);
bool generatePolkitPolicyFilename(const char* appImagePath, char* buffer, size_t bufferSize);
}

namespace {

// Embed the escaped text where the policy puts it and read it back with a
// real XML parser. Whatever the escaping does is correct exactly when what
// comes out equals what went in, which is a stronger statement than any list
// of substitutions.
//
// Element text is the position that matters -- hasPolkitPolicyForPath()
// searches for the path between `">` and `</annotate>` -- so that is the one
// checked for every path. The attribute is checked too, but only for paths
// without raw tab, newline or carriage return: XML normalises whitespace
// inside attribute values, so a tab there comes back as a space whatever the
// escaping did, and no escaping can prevent it.
struct RoundTrip {
    bool parsed = false;
    QString attribute;
    QString text;
};

RoundTrip roundTripThroughXml(const QString& raw)
{
    const QString escaped = PlatformQuirks::TestAPI::xmlEscape(raw);
    const QString doc = QStringLiteral(
        "<action id=\"org.test\"><annotate key=\"%1\">%1</annotate></action>")
        .arg(escaped);

    RoundTrip out;
    QXmlStreamReader reader(doc);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("annotate")) {
            out.attribute = reader.attributes().value(QStringLiteral("key")).toString();
            out.text = reader.readElementText();
        }
    }
    out.parsed = !reader.hasError();
    return out;
}

} // namespace

TEST_CASE("A path survives the policy document unchanged", "[platformquirks][polkit]")
{
    // The paths an AppImage can actually live at, hostile ones included. The
    // third is the one that matters: unescaped it would close the annotation
    // and add a rule of its own to a file that grants root.
    const QStringList paths = {
        QStringLiteral("/home/pi/Downloads/rpi-imager.AppImage"),
        QStringLiteral("/home/pi/rpi & imager.AppImage"),
        QStringLiteral("/tmp/x</annotate><allow_any>yes</allow_any><annotate key=\"y\">"),
        QStringLiteral("/home/o'brien/rpi-imager.AppImage"),
        QStringLiteral("/home/pi/\"quoted\"/rpi-imager.AppImage"),
        QStringLiteral("/home/pi/a<b>c/rpi-imager.AppImage"),
        QStringLiteral("/home/山田/rpi-imager.AppImage"),
        QStringLiteral("/home/pi/tab\there/rpi-imager.AppImage"),
    };

    for (const QString& raw : paths) {
        INFO("path: " << raw.toStdString());
        const RoundTrip rt = roundTripThroughXml(raw);

        REQUIRE(rt.parsed);
        CHECK(rt.text == raw);
        if (!raw.contains(QLatin1Char('\t')) && !raw.contains(QLatin1Char('\n'))
            && !raw.contains(QLatin1Char('\r')))
            CHECK(rt.attribute == raw);
    }
}

TEST_CASE("A control character does not make the policy unreadable",
          "[platformquirks][polkit]")
{
    // XML 1.0 has no way to carry most control characters, escaped or not, so
    // one arriving in a path has to be turned into a numeric reference. Left
    // raw the document does not parse, and a policy that does not parse is a
    // user who cannot write to a disk with no explanation anywhere.
    const QString raw = QStringLiteral("/home/pi/odd\x01name.AppImage");
    const QString escaped = PlatformQuirks::TestAPI::xmlEscape(raw);

    INFO("escaped: " << escaped.toStdString());
    CHECK_FALSE(escaped.contains(QChar(0x01)));
    CHECK_THAT(escaped.toStdString(), ContainsSubstring("&#x1;"));

    // Tab, newline and carriage return are the three XML does carry, and they
    // are left alone rather than expanded for no reason.
    const QString whitespace = QStringLiteral("a\tb\nc\rd");
    CHECK(PlatformQuirks::TestAPI::xmlEscape(whitespace) == whitespace);
}

TEST_CASE("Ordinary text is not disturbed", "[platformquirks][polkit]")
{
    // The common case, stated so an over-eager escape shows up: a policy file
    // full of entities for characters that never needed them is still valid
    // XML, so nothing else here would notice.
    const QString plain = QStringLiteral("/usr/local/bin/rpi-imager-2.0.1");
    CHECK(PlatformQuirks::TestAPI::xmlEscape(plain) == plain);
}

TEST_CASE("A policy filename cannot escape the actions directory",
          "[platformquirks][polkit]")
{
    // The name is written into /etc/polkit-1/actions, so a separator or a
    // parent reference surviving from the path would put the file somewhere
    // else entirely -- with root's permissions, since the installer runs
    // elevated.
    const char* const paths[] = {
        "/home/pi/rpi-imager.AppImage",
        "/home/pi/../../etc/passwd",
        "/home/pi/a/b/c/d/e/f/rpi-imager.AppImage",
        "relative/path.AppImage",
        "/home/pi/rpi imager (1).AppImage",
    };

    for (const char* path : paths) {
        char buffer[128] = {};
        INFO("path: " << path);
        REQUIRE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
            path, buffer, sizeof(buffer)));

        const QString name = QString::fromUtf8(buffer);
        INFO("name: " << name.toStdString());
        CHECK_FALSE(name.contains(QLatin1Char('/')));
        CHECK_FALSE(name.contains(QStringLiteral("..")));
        CHECK(name.startsWith(QStringLiteral("com.raspberrypi.rpi-imager.appimage-")));
        CHECK(name.endsWith(QStringLiteral(".policy")));
    }
}

TEST_CASE("Two AppImages in different places get different policies",
          "[platformquirks][polkit]")
{
    // One policy per location is the whole point of hashing the path: a
    // shared name means installing the second AppImage silently revokes the
    // first one's authorisation, and that one stops being able to write.
    char a[128] = {};
    char b[128] = {};
    REQUIRE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/home/pi/Downloads/rpi-imager.AppImage", a, sizeof(a)));
    REQUIRE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/opt/rpi-imager/rpi-imager.AppImage", b, sizeof(b)));

    CHECK(QString::fromUtf8(a) != QString::fromUtf8(b));
}

TEST_CASE("The same AppImage always gets the same policy name",
          "[platformquirks][polkit]")
{
    // The other half. Re-running from the same path has to land on the file
    // already there, or every launch leaves another stale policy granting
    // root to a path -- which is what the stale-policy cleanup exists to
    // undo, and it matches by name.
    char first[128] = {};
    char second[128] = {};
    REQUIRE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/home/pi/rpi-imager.AppImage", first, sizeof(first)));
    REQUIRE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/home/pi/rpi-imager.AppImage", second, sizeof(second)));

    CHECK(QString::fromUtf8(first) == QString::fromUtf8(second));
}

TEST_CASE("A filename is refused rather than truncated", "[platformquirks][polkit]")
{
    // Truncation would produce a name that is still a valid filename and no
    // longer unique to the path, which is the collision above arriving
    // silently. Refusing is the only safe answer.
    char small[64] = {};
    CHECK_FALSE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/home/pi/rpi-imager.AppImage", small, 32));

    // And the arguments it cannot work with at all.
    char buffer[128] = {};
    CHECK_FALSE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        nullptr, buffer, sizeof(buffer)));
    CHECK_FALSE(PlatformQuirks::TestAPI::generatePolkitPolicyFilename(
        "/home/pi/rpi-imager.AppImage", nullptr, sizeof(buffer)));
}
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
// ══════════════════════════════════════════════════════════════
// Reduced motion, as Raspberry Pi OS expresses it
//
// A user who has turned animations off in their desktop settings has asked
// for something, and every animation duration in the UI is gated on this
// answer. Getting it wrong is not cosmetic: the setting exists because
// motion makes some people ill, and ignoring it is ignoring an
// accessibility preference the user went and set.
//
// Three routes are tried in order -- GSettings for GNOME, kreadconfig for
// Plasma, and then ~/.config/gtk-3.0/settings.ini, which is the one that
// answers on Raspberry Pi OS, XFCE, MATE, LXDE and LXQt. The first two need
// a desktop's tooling installed to say anything; the third is a file, and it
// is the route this product's own desktop takes.
//
// HOME is redirected so the file under test is the only one in play, and put
// back afterwards.
// ══════════════════════════════════════════════════════════════

namespace {

class HomeRedirect
{
public:
    explicit HomeRedirect(const QString& path) : _saved(qgetenv("HOME"))
    {
        qputenv("HOME", path.toUtf8());
    }
    ~HomeRedirect() { qputenv("HOME", _saved); }

    HomeRedirect(const HomeRedirect&) = delete;
    HomeRedirect& operator=(const HomeRedirect&) = delete;

private:
    QByteArray _saved;
};

// Write ~/.config/gtk-3.0/settings.ini with the given body. Empty body means
// no file at all.
bool writeGtkSettings(const QString& home, const QString& body)
{
    const QString dir = home + QStringLiteral("/.config/gtk-3.0");
    if (!QDir().mkpath(dir))
        return false;
    const QString path = dir + QStringLiteral("/settings.ini");
    if (body.isEmpty())
        return !QFile::exists(path) || QFile::remove(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    return f.write(body.toUtf8()) == body.toUtf8().size();
}

} // namespace

#ifdef MOTION_PROBE_BINARY
// ══════════════════════════════════════════════════════════════
// Asking the desktop whether to animate.
//
// prefersReducedMotion consults three sources in turn: GNOME's
// enable-animations, KDE's AnimationDurationFactor, and the GTK settings
// file. Only the last had tests -- the first two run their tools by absolute
// path, deliberately, because this function can run as root after pkexec
// where a relative name would search a PATH the user controls.
//
// So these cases bind substitutes over /usr/bin/gsettings and over /usr/bin
// itself inside an unprivileged mount namespace. HOME is pointed at an empty
// directory throughout, so the GTK fallback cannot answer for a case about
// one of the other two.
//
// Getting this wrong in the "no" direction animates the window for somebody
// who asked it not to, and for some vestibular conditions that request is
// not a preference.
// ══════════════════════════════════════════════════════════════

namespace {

// Defined further down, with the other namespace-based fixtures.
bool haveMountNamespaces();

// The directory planted over /usr/bin also has to carry a shell: /bin is a
// symlink into /usr/bin on Debian and Raspberry Pi OS, so the bind hides
// /bin/sh along with everything else, and a #!/bin/sh script then has no
// interpreter. Without this the planted tools do not run and every case
// reads as "the desktop said nothing" -- which several of them expect, so
// they would pass for the wrong reason.
bool plantShell(const QString& binDir)
{
    const QString real = QFileInfo(QStringLiteral("/bin/sh")).canonicalFilePath();
    if (real.isEmpty())
        return false;
    const QString dest = binDir + QStringLiteral("/sh");
    QFile::remove(dest);
    if (!QFile::copy(real, dest))
        return false;
    return QFile(dest).setPermissions(
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
        QFileDevice::ReadGroup | QFileDevice::ExeGroup |
        QFileDevice::ReadOther | QFileDevice::ExeOther);
}

// A shell script that answers on stdout and nothing else.
bool plantTool(const QString& path, const QByteArray& output)
{
    const QByteArray script = "#!/bin/sh\nprintf '%s\\n' \"" + output + "\"\n";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (f.write(script) != script.size())
        return false;
    f.close();
    return f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                            QFileDevice::ExeGroup | QFileDevice::ReadOther |
                            QFileDevice::ExeOther);
}

// Run the probe with `binDir` bound over /usr/bin and `home` as HOME. -1 if
// the probe could not be run at all.
//
// Binding the whole directory rather than a file over each tool is what lets
// a case supply kreadconfig6, which is not installed on most machines and so
// has nothing to bind onto. Nothing in /usr/bin is needed after the exec.
int reducedMotionWith(const QString& binDir, const QString& home)
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             // export rather than `env`: /usr/bin/env is one of the things
             // the bind has just hidden.
             QStringLiteral("mount --bind \"$1\" /usr/bin "
                            "&& export HOME=\"$3\" && exec \"$2\""),
             QStringLiteral("_"), binDir,
             QStringLiteral(MOTION_PROBE_BINARY), home});
    if (!p.waitForFinished(30000))
        return -1;
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    if (out.contains(QStringLiteral("REDUCED=1")))
        return 1;
    if (out.contains(QStringLiteral("REDUCED=0")))
        return 0;
    return -1;
}

} // namespace

TEST_CASE("Animations turned off in GNOME are respected",
          "[platformquirks][a11y][motion]")
{
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable, so /usr/bin "
             "cannot be substituted");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    const QString home = tmp.filePath(QStringLiteral("home"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(QDir().mkpath(home));
    REQUIRE(plantShell(bin));

    SECTION("enable-animations false")
    {
        REQUIRE(plantTool(bin + QStringLiteral("/gsettings"), "false"));
        CHECK(reducedMotionWith(bin, home) == 1);
    }

    SECTION("enable-animations true")
    {
        REQUIRE(plantTool(bin + QStringLiteral("/gsettings"), "true"));
        CHECK(reducedMotionWith(bin, home) == 0);
    }

    SECTION("a schema that is not installed answers nothing")
    {
        // gsettings prints its complaint to stderr and nothing to stdout.
        // Not an instruction either way, so the search moves on.
        REQUIRE(plantTool(bin + QStringLiteral("/gsettings"), ""));
        CHECK(reducedMotionWith(bin, home) == 0);
    }
}

TEST_CASE("Animations turned off in KDE Plasma are respected",
          "[platformquirks][a11y][motion]")
{
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    const QString home = tmp.filePath(QStringLiteral("home"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(QDir().mkpath(home));
    REQUIRE(plantShell(bin));

    SECTION("AnimationDurationFactor of zero")
    {
        // How Plasma records "disable animations" -- there is no boolean.
        REQUIRE(plantTool(bin + QStringLiteral("/kreadconfig6"), "0"));
        CHECK(reducedMotionWith(bin, home) == 1);
    }

    SECTION("the default factor of one")
    {
        REQUIRE(plantTool(bin + QStringLiteral("/kreadconfig6"), "1"));
        CHECK(reducedMotionWith(bin, home) == 0);
    }

    SECTION("a slowed-down but not disabled factor")
    {
        // Only exactly zero means off. A user who has slowed animations down
        // still wants to see them.
        REQUIRE(plantTool(bin + QStringLiteral("/kreadconfig6"), "0.5"));
        CHECK(reducedMotionWith(bin, home) == 0);
    }

    SECTION("Plasma 5, where the tool is called kreadconfig5")
    {
        // Still shipping on Debian bookworm, which Raspberry Pi OS is built
        // from. Falling back to it is the difference between honouring the
        // setting and ignoring it on that whole generation of desktop.
        REQUIRE(plantTool(bin + QStringLiteral("/kreadconfig5"), "0"));
        CHECK(reducedMotionWith(bin, home) == 1);
    }

    SECTION("Plasma 6 is asked first where both are installed")
    {
        REQUIRE(plantTool(bin + QStringLiteral("/kreadconfig6"), "0"));
        REQUIRE(plantTool(bin + QStringLiteral("/kreadconfig5"), "1"));
        CHECK(reducedMotionWith(bin, home) == 1);
    }
}

TEST_CASE("A desktop with none of the three says nothing",
          "[platformquirks][a11y][motion]")
{
    // No gsettings, no kreadconfig, no GTK settings file. Animations stay on,
    // which is the right default: nobody has asked for them to be off.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    const QString home = tmp.filePath(QStringLiteral("home"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(QDir().mkpath(home));
    REQUIRE(plantShell(bin));

    CHECK(reducedMotionWith(bin, home) == 0);
}

TEST_CASE("A tool planted on PATH is not the one consulted",
          "[platformquirks][a11y][motion]")
{
    // The absolute paths are there because this can run as root after
    // pkexec. A gsettings on PATH saying "false" must not be reached: at that
    // point whoever set PATH before the elevation is choosing what runs as
    // root, and the answer to this question is the least of it.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));       // becomes /usr/bin
    const QString onPath = tmp.filePath(QStringLiteral("onpath")); // just on PATH
    const QString home = tmp.filePath(QStringLiteral("home"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(QDir().mkpath(onPath));
    REQUIRE(QDir().mkpath(home));
    REQUIRE(plantShell(bin));
    REQUIRE(plantShell(onPath));

    // Nothing at the absolute paths, and a liar on PATH.
    REQUIRE(plantTool(onPath + QStringLiteral("/gsettings"), "false"));
    REQUIRE(plantTool(onPath + QStringLiteral("/kreadconfig6"), "0"));

    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral("mount --bind \"$1\" /usr/bin "
                            "&& export HOME=\"$3\" PATH=\"$4\" && exec \"$2\""),
             QStringLiteral("_"), bin, QStringLiteral(MOTION_PROBE_BINARY),
             home, onPath});
    REQUIRE(p.waitForFinished(30000));
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    INFO(out.toStdString());
    CHECK(out.contains(QStringLiteral("REDUCED=0")));
}
#endif // MOTION_PROBE_BINARY

TEST_CASE("Animations turned off in the GTK settings are respected",
          "[platformquirks][motion]")
{
    QTemporaryDir home;
    REQUIRE(home.isValid());
    HomeRedirect redirect(home.path());

    // With no settings file the answer comes from the two routes ahead of
    // this one. If the desktop running the suite has already asked for
    // reduced motion there is nothing this file could add, and no assertion
    // below could tell the routes apart.
    REQUIRE(writeGtkSettings(home.path(), QString()));
    if (PlatformQuirks::prefersReducedMotion())
        SKIP("this desktop already reports reduced motion through GSettings or "
             "kreadconfig, so the GTK file cannot be observed");

    struct Row {
        const char* tag;
        QString body;
        bool expected;
    };

    const Row rows[] = {
        // The two spellings a user or a settings editor will produce.
        {"zero", QStringLiteral("[Settings]\ngtk-enable-animations=0\n"), true},
        {"false", QStringLiteral("[Settings]\ngtk-enable-animations=false\n"), true},
        // Spaces around the separator are ordinary in these files.
        {"spaced", QStringLiteral("[Settings]\ngtk-enable-animations = 0\n"), true},
        // Animations on, said either way: the preference is not set, so the
        // UI is free to animate.
        {"one", QStringLiteral("[Settings]\ngtk-enable-animations=1\n"), false},
        {"true", QStringLiteral("[Settings]\ngtk-enable-animations=true\n"), false},
        // A file that says nothing about animations.
        {"unrelated", QStringLiteral("[Settings]\ngtk-theme-name=Adwaita\n"), false},
        // The key is not the first line, which is the usual shape of a real
        // settings.ini.
        {"later line",
         QStringLiteral("[Settings]\ngtk-theme-name=Adwaita\n"
                        "gtk-font-name=Sans 11\ngtk-enable-animations=0\n"),
         true},
        // Leading whitespace, as an editor may leave it.
        {"indented", QStringLiteral("[Settings]\n  gtk-enable-animations=0\n"), true},
        // A longer key beginning the same way is a different key. Read as a
        // prefix it both answered for the wrong setting and stopped the
        // search, so the real line underneath was never reached -- a
        // preference set and silently dropped.
        {"lookalike key above the real one",
         QStringLiteral("[Settings]\ngtk-enable-animationsX=1\n"
                        "gtk-enable-animations=0\n"),
         true},
        {"lookalike key alone",
         QStringLiteral("[Settings]\ngtk-enable-animationsX=0\n"), false},
    };

    for (const Row& row : rows) {
        INFO(row.tag);
        REQUIRE(writeGtkSettings(home.path(), row.body));
        CHECK(PlatformQuirks::prefersReducedMotion() == row.expected);
    }
}

TEST_CASE("A GTK settings file that cannot be parsed leaves animations on",
          "[platformquirks][motion]")
{
    // Nothing here should be read as a request for reduced motion. Guessing
    // "off" from a malformed file would disable animations for someone who
    // never asked, which is the same class of mistake in the other
    // direction.
    QTemporaryDir home;
    REQUIRE(home.isValid());
    HomeRedirect redirect(home.path());

    REQUIRE(writeGtkSettings(home.path(), QString()));
    if (PlatformQuirks::prefersReducedMotion())
        SKIP("this desktop already reports reduced motion by another route");

    const QStringList bodies = {
        QStringLiteral(""),                                   // present but empty
        QStringLiteral("gtk-enable-animations"),              // no separator at all
        QStringLiteral("[Settings]\ngtk-enable-animations=\n"),   // nothing after it
        QStringLiteral("not an ini file at all\n"),
        QStringLiteral("[Settings]\ngtk-enable-animationsX=0\n"),  // a different key
    };

    for (const QString& body : bodies) {
        INFO("body: " << body.toStdString());
        // An empty body means "no file"; write a single newline instead so
        // the file exists and is empty.
        REQUIRE(writeGtkSettings(home.path(),
                                 body.isEmpty() ? QStringLiteral("\n") : body));
        CHECK_FALSE(PlatformQuirks::prefersReducedMotion());
    }
}

TEST_CASE("No GTK settings file is not a request for reduced motion",
          "[platformquirks][motion]")
{
    // The default on a fresh install: the file does not exist, and the
    // absence of a preference is not a preference.
    QTemporaryDir home;
    REQUIRE(home.isValid());
    HomeRedirect redirect(home.path());
    REQUIRE(writeGtkSettings(home.path(), QString()));

    const bool before = PlatformQuirks::prefersReducedMotion();
    if (before)
        SKIP("this desktop reports reduced motion by another route");

    CHECK_FALSE(PlatformQuirks::prefersReducedMotion());
}
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
// ══════════════════════════════════════════════════════════════
// The rpi-imager:// handler, after the first time
//
// The desktop entry is what makes a rpi-imager:// link open Imager with the
// link as an argument. Writing it fresh is covered above. What was not is
// everything after that: an entry left by an older install at a path the
// executable has moved away from, and a re-registration that cannot be
// written at all.
//
// Both matter to somebody following a link. A stale entry names an
// executable that is no longer there, so the link opens nothing; and a
// failed rewrite that took the working entry with it turns a link that works
// into one that does not.
// ══════════════════════════════════════════════════════════════

namespace {

// XDG_DATA_HOME and XDG_CONFIG_HOME pointed at temporary directories, put
// back on the way out, so nothing here touches the real desktop database.
class XdgRedirect
{
public:
    XdgRedirect(const QString& data, const QString& config)
        : _data(qgetenv("XDG_DATA_HOME")), _config(qgetenv("XDG_CONFIG_HOME"))
    {
        qputenv("XDG_DATA_HOME", data.toUtf8());
        qputenv("XDG_CONFIG_HOME", config.toUtf8());
    }
    ~XdgRedirect()
    {
        if (_data.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", _data);
        if (_config.isNull()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", _config);
    }

    XdgRedirect(const XdgRedirect&) = delete;
    XdgRedirect& operator=(const XdgRedirect&) = delete;

private:
    QByteArray _data, _config;
};

QString uriHandlerPath(const QString& dataHome)
{
    return dataHome
        + QStringLiteral("/applications/com.raspberrypi.rpi-imager-uri-handler.desktop");
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

} // namespace

TEST_CASE("A handler left by an older install is replaced", "[platformquirks][uri]")
{
    // What an upgrade or a moved AppImage leaves behind: an entry whose Exec
    // names a path this executable is no longer at. Following a link then
    // launches nothing at all, and nothing about the failure points here.
    QTemporaryDir dataHome, configHome;
    REQUIRE(dataHome.isValid());
    REQUIRE(configHome.isValid());
    XdgRedirect redirect(dataHome.path(), configHome.path());

    const QString path = uriHandlerPath(dataHome.path());
    REQUIRE(QDir().mkpath(QFileInfo(path).path()));
    {
        QFile stale(path);
        REQUIRE(stale.open(QIODevice::WriteOnly));
        stale.write(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=Raspberry Pi Imager\n"
            "Exec=/opt/some-old-location/rpi-imager.AppImage %u\n"
            "MimeType=x-scheme-handler/rpi-imager;\n");
    }

    CHECK(PlatformQuirks::registerUriScheme() == true);

    const QString written = QString::fromUtf8(readAll(path));
    INFO(written.toStdString());
    CHECK_FALSE(written.contains(QStringLiteral("/opt/some-old-location/")));
    // Naming this executable, and still passing the URL on.
    CHECK(written.contains(QString::fromUtf8(PlatformQuirks::getBundlePath())));
    CHECK(written.contains(QStringLiteral("%u")));
    CHECK(written.contains(QStringLiteral("MimeType=x-scheme-handler/rpi-imager;")));
}

TEST_CASE("A registration that cannot be written keeps the entry that worked",
          "[platformquirks][uri]")
{
    // The rewrite fails -- an immutable home, a full disk, a directory the
    // user no longer owns. Losing the entry already there would turn a link
    // that works into one that does nothing, which is worse than leaving the
    // old one in place.
    if (::geteuid() == 0)
        SKIP("root ignores the directory permissions this relies on");

    QTemporaryDir dataHome, configHome;
    REQUIRE(dataHome.isValid());
    REQUIRE(configHome.isValid());
    XdgRedirect redirect(dataHome.path(), configHome.path());

    // A working entry, written by the code itself.
    REQUIRE(PlatformQuirks::registerUriScheme() == true);
    const QString path = uriHandlerPath(dataHome.path());
    const QByteArray working = readAll(path);
    REQUIRE_FALSE(working.isEmpty());

    // Now make it stale *and* the directory unwritable, so the rewrite is
    // attempted and cannot succeed.
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[Desktop Entry]\nExec=/gone %u\n");
    }
    const QString appsDir = QFileInfo(path).path();
    struct Restore {
        QString dir;
        ~Restore() { QFile::setPermissions(dir, QFileDevice::ReadOwner
                                                | QFileDevice::WriteOwner
                                                | QFileDevice::ExeOwner); }
    } restore{appsDir};
    REQUIRE(QFile::setPermissions(appsDir,
                                  QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    CHECK(PlatformQuirks::registerUriScheme() == false);

    // The file that was there is still there and still whole -- not truncated
    // by a write that could not finish.
    const QByteArray after = readAll(path);
    CHECK(after == QByteArray("[Desktop Entry]\nExec=/gone %u\n"));
}

TEST_CASE("A successful registration leaves no half-written files behind",
          "[platformquirks][uri]")
{
    // The write goes through QSaveFile, which works via a temporary beside
    // the target. One left behind would be picked up by a desktop database
    // scan as another handler for the same scheme.
    QTemporaryDir dataHome, configHome;
    REQUIRE(dataHome.isValid());
    REQUIRE(configHome.isValid());
    XdgRedirect redirect(dataHome.path(), configHome.path());

    REQUIRE(PlatformQuirks::registerUriScheme() == true);

    const QString entry =
        QStringLiteral("com.raspberrypi.rpi-imager-uri-handler.desktop");
    const QString appsDir = QFileInfo(uriHandlerPath(dataHome.path())).path();
    const QStringList left = QDir(appsDir).entryList(QDir::Files | QDir::Hidden);
    INFO(left.join(QStringLiteral(", ")).toStdString());

    CHECK(left.contains(entry));
    for (const QString& name : left) {
        INFO("file: " << name.toStdString());
        // update-desktop-database writes mimeinfo.cache here and is welcome
        // to; what must not survive is a QSaveFile temporary, which is the
        // target name with a suffix, or a hidden file beside it.
        CHECK_FALSE((name.startsWith(entry) && name != entry));
        CHECK_FALSE(name.startsWith(QLatin1Char('.')));
    }
}
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
#ifdef ELEVATION_PROBE_BINARY
// ══════════════════════════════════════════════════════════════
// Running as root on somebody else's behalf
//
// Elevated through sudo or pkexec, Imager is root but is acting for the user
// who started it. applyQuirks() is what repoints HOME and the XDG
// directories back at them. Without it every setting, the OS list cache and
// the downloaded image land under /root: the user's preferences are gone on
// the next launch, and the cache is rewritten as files they cannot read --
// which is also how the configuration file ends up owned by root, the
// condition image_writer_test has a case for repairing.
//
// None of it runs unless euid is 0, so it cannot be reached from this
// binary. A probe calls applyQuirks() and prints the environment before and
// after; these cases run it under sudo and read that back. Without
// passwordless sudo they skip.
//
// DISPLAY is removed and WAYLAND_DISPLAY set for every run. With neither,
// applyQuirks() looks for an X11 socket and, finding one, calls xhost to
// grant root access to the user's display -- a change to the session of
// whoever is running the suite, which no test should be making.
// ══════════════════════════════════════════════════════════════

namespace {

struct ProbeRun
{
    bool finished = false;
    int exitCode = -1;
    QString out;
    QString err;

    QString value(const QString& key) const
    {
        for (const QString& line : out.split(QLatin1Char('\n'))) {
            if (line.startsWith(key + QLatin1Char('=')))
                return line.mid(key.size() + 1);
        }
        return {};
    }
};

// Run the probe as root, with `unset` removed from its environment and
// `assign` set in it. Applied inside the elevated process with env(1), so they
// win over what sudo itself sets -- and every -u has to precede the
// assignments, or env takes the next one for the command name.
ProbeRun runProbeAsRoot(const QStringList& unset, const QStringList& assign)
{
    ProbeRun r;
    QStringList args{QStringLiteral("-n"), QStringLiteral("env")};
    for (const QString& name : QStringList{QStringLiteral("DISPLAY")} + unset)
        args << QStringLiteral("-u") << name;
    args << QStringLiteral("WAYLAND_DISPLAY=wayland-test");
    args += assign;
    args << QStringLiteral(ELEVATION_PROBE_BINARY);

    QProcess p;
    p.start(QStringLiteral("sudo"), args);
    if (!p.waitForFinished(30000))
        return r;
    r.finished = true;
    r.exitCode = p.exitCode();
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    r.err = QString::fromUtf8(p.readAllStandardError());
    return r;
}

bool havePasswordlessSudo()
{
    QProcess probe;
    probe.start(QStringLiteral("sudo"),
                {QStringLiteral("-n"), QStringLiteral("true")});
    probe.waitForFinished(10000);
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

} // namespace

TEST_CASE("Elevated through sudo, the user's own directories are used",
          "[platformquirks][elevation][root]")
{
    if (!havePasswordlessSudo())
        SKIP("passwordless sudo is not available, so the elevated path cannot "
             "be reached");

    const QString home = QDir::homePath();
    const unsigned long uid = static_cast<unsigned long>(::getuid());

    // sudo sets SUDO_UID to the invoking user, which is the real scenario.
    const ProbeRun r = runProbeAsRoot({}, {});
    INFO("stdout:\n" << r.out.toStdString() << "\nstderr:\n" << r.err.toStdString());
    REQUIRE(r.finished);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.value(QStringLiteral("EUID")) == QStringLiteral("0"));

    // Root's home on the way in, the user's on the way out. This is the whole
    // point: QSettings and QStandardPaths read HOME.
    CHECK(r.value(QStringLiteral("AFTER_HOME")) == home);
    CHECK(r.value(QStringLiteral("AFTER_XDG_CACHE_HOME")) == home + QStringLiteral("/.cache"));
    CHECK(r.value(QStringLiteral("AFTER_XDG_CONFIG_HOME")) == home + QStringLiteral("/.config"));
    CHECK(r.value(QStringLiteral("AFTER_XDG_DATA_HOME")) == home + QStringLiteral("/.local/share"));

    // The runtime directory and the bus address derived from it: without
    // these the portal file dialogs and the suspend inhibitor have nothing
    // to talk to, so a write can be interrupted by the machine sleeping.
    const QString runtime = QStringLiteral("/run/user/%1").arg(uid);
    CHECK(r.value(QStringLiteral("AFTER_XDG_RUNTIME_DIR")) == runtime);
    CHECK(r.value(QStringLiteral("AFTER_DBUS_SESSION_BUS_ADDRESS"))
          == QStringLiteral("unix:path=%1/bus").arg(runtime));
}

TEST_CASE("Elevated through pkexec, the same handover happens",
          "[platformquirks][elevation][root]")
{
    // The AppImage route. pkexec sets PKEXEC_UID instead, and sudo is not in
    // the picture -- so SUDO_UID is removed to make sure it is PKEXEC_UID
    // being read and not the other one still lying around.
    if (!havePasswordlessSudo())
        SKIP("passwordless sudo is not available");

    const QString home = QDir::homePath();
    const ProbeRun r = runProbeAsRoot(
        {QStringLiteral("SUDO_UID")},
        {QStringLiteral("PKEXEC_UID=%1").arg(::getuid())});

    INFO("stdout:\n" << r.out.toStdString() << "\nstderr:\n" << r.err.toStdString());
    REQUIRE(r.finished);
    CHECK(r.value(QStringLiteral("AFTER_HOME")) == home);
    CHECK(r.value(QStringLiteral("AFTER_XDG_CONFIG_HOME")) == home + QStringLiteral("/.config"));
    CHECK_THAT(r.err.toStdString(), ContainsSubstring("pkexec"));
}

TEST_CASE("A UID that is not a number is refused, not guessed at",
          "[platformquirks][elevation][root]")
{
    // The value comes from the environment, which on this path is attacker
    // influenced -- it is the one thing a caller controls while the process
    // is root. Anything but a clean number has to be refused: the comments
    // in the code single out an overflow of uid_t, and trailing garbage is
    // what atoi() would have silently accepted.
    if (!havePasswordlessSudo())
        SKIP("passwordless sudo is not available");

    struct Row { const char* tag; QString value; };
    const Row rows[] = {
        {"trailing garbage", QStringLiteral("1000x")},
        {"leading text", QStringLiteral("x1000")},
        {"empty", QStringLiteral("")},
        {"not a number", QStringLiteral("root")},
        {"overflows uid_t", QStringLiteral("4294967296")},
        {"far past any uid", QStringLiteral("99999999999999999999")},
        {"negative", QStringLiteral("-1000")},
    };

    for (const Row& row : rows) {
        INFO(row.tag);
        const ProbeRun r = runProbeAsRoot(
            {QStringLiteral("PKEXEC_UID")},
            {QStringLiteral("SUDO_UID=%1").arg(row.value)});
        REQUIRE(r.finished);
        INFO("stdout:\n" << r.out.toStdString() << "\nstderr:\n" << r.err.toStdString());

        // HOME is left exactly as it was rather than pointed at whichever
        // user that value happened to land on.
        CHECK(r.value(QStringLiteral("AFTER_HOME"))
              == r.value(QStringLiteral("BEFORE_HOME")));
        // And it is not silently ignored: something in the log says why.
        CHECK_THAT(r.err.toStdString(), ContainsSubstring("WARNING"));
    }
}

TEST_CASE("With both elevation variables set, sudo's is the one used",
          "[platformquirks][elevation][root]")
{
    // Rare, but reachable: `sudo -E` into a shell that then launches an
    // AppImage which self-elevates through pkexec, or any wrapper that
    // exports both. applyQuirks() reads SUDO_UID first and PKEXEC_UID only
    // if that is absent.
    //
    // Worth pinning because the other half of this file disagrees.
    // resolveOriginalUid, which decides whose desktop session a link opens
    // on, reads PKEXEC_UID first -- see "The user behind an elevated session
    // is recognised". So with both set, the settings would be written to one
    // user's home while the browser opened on another's session. Neither
    // order is obviously wrong and changing either would move where settings
    // land for whoever does have both set, so this records the behaviour
    // rather than choosing between them.
    if (!havePasswordlessSudo())
        SKIP("passwordless sudo is not available");

    // A second account with a home directory that is neither root's nor the
    // invoking user's, so which variable won is visible in the answer.
    struct passwd* other = ::getpwnam("daemon");
    if (!other || !other->pw_dir)
        SKIP("no second account to distinguish the two variables with");
    const QString otherHome = QString::fromUtf8(other->pw_dir);
    if (otherHome == QDir::homePath())
        SKIP("the second account shares the invoking user's home directory");

    const ProbeRun r = runProbeAsRoot(
        {},
        {QStringLiteral("SUDO_UID=%1").arg(other->pw_uid),
         QStringLiteral("PKEXEC_UID=%1").arg(::getuid())});

    REQUIRE(r.finished);
    INFO("stdout:\n" << r.out.toStdString() << "\nstderr:\n" << r.err.toStdString());

    CHECK(r.value(QStringLiteral("AFTER_HOME")) == otherHome);
    CHECK(r.value(QStringLiteral("AFTER_XDG_CONFIG_HOME"))
          == otherHome + QStringLiteral("/.config"));
    CHECK_THAT(r.err.toStdString(), ContainsSubstring("sudo"));
}

TEST_CASE("A UID with no account behind it is refused",
          "[platformquirks][elevation][root]")
{
    // A well-formed number that no longer names a user -- an account deleted
    // between login and launch, or a container without the passwd entry.
    // Guessing a home directory for it would put the settings somewhere
    // nobody owns.
    if (!havePasswordlessSudo())
        SKIP("passwordless sudo is not available");

    const ProbeRun r = runProbeAsRoot(
        {QStringLiteral("PKEXEC_UID")},
        {QStringLiteral("SUDO_UID=4294967000")});

    REQUIRE(r.finished);
    INFO("stdout:\n" << r.out.toStdString() << "\nstderr:\n" << r.err.toStdString());
    CHECK(r.value(QStringLiteral("AFTER_HOME"))
          == r.value(QStringLiteral("BEFORE_HOME")));
    CHECK_THAT(r.err.toStdString(), ContainsSubstring("UID"));
}

TEST_CASE("Run without elevation, nothing is repointed",
          "[platformquirks][elevation]")
{
    // The ordinary case, and the one that needs no sudo: not root, so there
    // is no other user to act for and the environment is left alone.
    if (::geteuid() == 0)
        SKIP("running as root, so this is the elevated path instead");

    QProcess p;
    p.start(QStringLiteral(ELEVATION_PROBE_BINARY), {});
    REQUIRE(p.waitForFinished(30000));

    ProbeRun r;
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    INFO(r.out.toStdString());
    CHECK(r.value(QStringLiteral("AFTER_HOME"))
          == r.value(QStringLiteral("BEFORE_HOME")));
    CHECK(r.value(QStringLiteral("AFTER_XDG_CONFIG_HOME")).isEmpty());
}
#endif // ELEVATION_PROBE_BINARY
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
#ifdef NETWORK_PROBE_BINARY
// ══════════════════════════════════════════════════════════════
// Whether Imager thinks you are online
//
// The answer gates the whole first half of the wizard: online it fetches the
// device and OS lists, offline it shows the placeholder that tells the user
// to check their connection and offers Retry. Getting it wrong in either
// direction is visible immediately -- a machine that is connected sitting on
// the offline screen, or one that is not spending the fetch timeout before
// saying so.
//
// It is decided by walking /sys/class/net and reading each interface's
// operstate. That path is hardcoded, so these cases bind-mount a synthetic
// one over it inside an unprivileged mount namespace and run a probe inside,
// which is the technique embedded_scaling/run.sh already uses for
// /sys/class/drm.
//
// PATH is emptied for the probe as well. With every interface down the code
// falls through to `nmcli networking connectivity check`, and on a machine
// where NetworkManager says "full" that would answer for the real network
// rather than the fixture -- so nmcli is made unfindable and the fixture is
// the only thing speaking.
// ══════════════════════════════════════════════════════════════

namespace {

bool haveMountNamespaces()
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("true")});
    if (!p.waitForFinished(10000))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// One directory per interface, each holding an operstate file.
bool buildNetFixture(const QString& root,
                     const QList<QPair<QString, QString>>& interfaces)
{
    for (const auto& iface : interfaces) {
        const QString dir = root + QLatin1Char('/') + iface.first;
        if (!QDir().mkpath(dir))
            return false;
        QFile f(dir + QStringLiteral("/operstate"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        const QByteArray body = (iface.second + QLatin1Char('\n')).toUtf8();
        if (f.write(body) != body.size())
            return false;
    }
    return true;
}

// Answer from the probe with `fixture` mounted over /sys/class/net. -1 if the
// probe could not be run at all.
//
// The probe's PATH is pointed at an empty directory, which is what keeps
// nmcli out of the picture. Unsetting PATH does not: QStandardPaths::
// findExecutable falls back to a built-in default and finds the real
// /usr/bin/nmcli, which then answers for the machine's actual network -- with
// every "online" case here passing whatever the fixture said. The outer PATH
// is left alone so unshare and sh can still be found; the substitution
// happens inside the namespace, on the probe only.
int connectivityWith(const QString& fixture, const QString& emptyBin)
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral("mount --bind \"$1\" /sys/class/net "
                            "&& exec env PATH=\"$3\" \"$2\""),
             QStringLiteral("_"), fixture,
             QStringLiteral(NETWORK_PROBE_BINARY), emptyBin});
    if (!p.waitForFinished(30000))
        return -1;
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    if (out.contains(QStringLiteral("CONNECTIVITY=1")))
        return 1;
    if (out.contains(QStringLiteral("CONNECTIVITY=0")))
        return 0;
    return -1;
}

} // namespace

TEST_CASE("An interface that is up means the OS list can be fetched",
          "[platformquirks][netdetect]")
{
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable, so /sys/class/net "
             "cannot be replaced");

    QTemporaryDir fixture, emptyBin;
    REQUIRE(fixture.isValid());
    REQUIRE(emptyBin.isValid());
    REQUIRE(buildNetFixture(fixture.path(), {
        {QStringLiteral("lo"), QStringLiteral("unknown")},
        {QStringLiteral("eth0"), QStringLiteral("up")},
    }));

    CHECK(connectivityWith(fixture.path(), emptyBin.path()) == 1);
}

TEST_CASE("Every interface down means the offline screen", "[platformquirks][netdetect]")
{
    // A cable out and Wi-Fi off. The wizard has to say so rather than
    // spending the fetch timeout first.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir fixture, emptyBin;
    REQUIRE(fixture.isValid());
    REQUIRE(emptyBin.isValid());
    REQUIRE(buildNetFixture(fixture.path(), {
        {QStringLiteral("lo"), QStringLiteral("unknown")},
        {QStringLiteral("eth0"), QStringLiteral("down")},
        {QStringLiteral("wlan0"), QStringLiteral("down")},
    }));

    CHECK(connectivityWith(fixture.path(), emptyBin.path()) == 0);
}

TEST_CASE("Loopback on its own is not a network", "[platformquirks][netdetect]")
{
    // lo is up on every machine ever booted. Counting it would report every
    // user as online, and the offline screen -- and the Retry button on it --
    // would never appear at all.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir fixture, emptyBin;
    REQUIRE(fixture.isValid());
    REQUIRE(emptyBin.isValid());
    REQUIRE(buildNetFixture(fixture.path(), {
        {QStringLiteral("lo"), QStringLiteral("up")},
    }));

    CHECK(connectivityWith(fixture.path(), emptyBin.path()) == 0);
}

TEST_CASE("A VLAN interface counts like any other", "[platformquirks][netdetect]")
{
    // eth0.100 is how a VLAN sub-interface is named, and the dot is why the
    // name check allows one. Rejected, a machine whose only route is over a
    // VLAN would be told it has no network.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir fixture, emptyBin;
    REQUIRE(fixture.isValid());
    REQUIRE(emptyBin.isValid());
    REQUIRE(buildNetFixture(fixture.path(), {
        {QStringLiteral("lo"), QStringLiteral("unknown")},
        {QStringLiteral("eth0.100"), QStringLiteral("up")},
    }));

    CHECK(connectivityWith(fixture.path(), emptyBin.path()) == 1);
}

TEST_CASE("An interface name that is not one is skipped", "[platformquirks][netdetect]")
{
    // The name is interpolated into /sys/class/net/%1/operstate, so it is
    // checked before use, and refusing means the entry does not count -- a
    // fixture holding nothing else reads as offline.
    //
    // Worth being exact about which row tests what. Only the over-long name
    // reaches the check: entryList() is called without QDir::Hidden, so a
    // directory whose name starts with a dot is filtered out before the check
    // sees it -- run against a fixture holding one, the code prints no
    // "skipping" warning at all. Those rows still say something worth
    // pinning, that a hidden directory does not count as an interface, but it
    // is entryList() making that true and not the guard. The guard's
    // leading-dot arm is unreachable from this call site, and a name
    // containing a separator cannot be a directory in the first place.
    //
    // Removing the check fails this case on the long-name row.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    const QList<QPair<QString, QString>> hostile = {
        {QStringLiteral(".hidden"), QStringLiteral("up")},
        {QStringLiteral("..sneaky"), QStringLiteral("up")},
        {QStringLiteral("aninterfacenamewaytoolongforifnamsiz"), QStringLiteral("up")},
    };

    QTemporaryDir emptyBin;
    REQUIRE(emptyBin.isValid());
    for (const auto& iface : hostile) {
        INFO("interface: " << iface.first.toStdString());
        QTemporaryDir fixture;
        REQUIRE(fixture.isValid());
        REQUIRE(buildNetFixture(fixture.path(), {
            {QStringLiteral("lo"), QStringLiteral("unknown")},
            iface,
        }));

        CHECK(connectivityWith(fixture.path(), emptyBin.path()) == 0);
    }
}

TEST_CASE("A legitimate name alongside a rejected one still counts",
          "[platformquirks][netdetect]")
{
    // The rejection skips one entry rather than abandoning the walk, so a
    // real interface listed after a bad name is still found. Giving up on the
    // first oddity would report a connected machine as offline.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir fixture, emptyBin;
    REQUIRE(fixture.isValid());
    REQUIRE(emptyBin.isValid());
    REQUIRE(buildNetFixture(fixture.path(), {
        {QStringLiteral(".hidden"), QStringLiteral("up")},
        {QStringLiteral("lo"), QStringLiteral("unknown")},
        {QStringLiteral("wlan0"), QStringLiteral("up")},
    }));

    CHECK(connectivityWith(fixture.path(), emptyBin.path()) == 1);
}

// ══════════════════════════════════════════════════════════════
// Whether the clock is trustworthy enough to fetch the OS list.
//
// In embedded mode -- Imager booted on the Pi itself -- isOnline() will not
// fetch anything until isNetworkReady() says yes, and it keeps polling until
// it does. A Pi has no battery-backed clock, so it boots in 1970; TLS to
// downloads.raspberrypi.com fails with a certificate that is not yet valid,
// which surfaces as a fetch error rather than anything a user could act on.
// Hence the wait for systemd-timesyncd.
//
// The failure this guards against is the opposite one: an answer of "not
// ready" that never becomes "ready" leaves the user looking at an empty list
// of operating systems for as long as they care to wait, with no error and
// no Retry.
//
// The three paths it consults are hardcoded, so each case bind-mounts its
// own over them inside an unprivileged mount namespace.
// ══════════════════════════════════════════════════════════════

namespace {

// A /sys/class/net fixture with one interface that is up, so the connectivity
// check ahead of the clock check passes and the clock is what is being
// measured.
bool buildOnlineNetFixture(const QString& root)
{
    return buildNetFixture(root, {{QStringLiteral("eth0"), QStringLiteral("up")}});
}

// -1 if the probe could not be run at all.
int networkReadyWith(const QString& netFixture, const QString& libSystemd,
                     const QString& varLibSystemd, const QString& emptyBin)
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral("mount --bind \"$1\" /sys/class/net "
                            "&& mount --bind \"$2\" /lib/systemd "
                            "&& mount --bind \"$3\" /var/lib/systemd "
                            "&& exec env PATH=\"$5\" \"$4\" ready"),
             QStringLiteral("_"), netFixture, libSystemd, varLibSystemd,
             QStringLiteral(NETWORK_PROBE_BINARY), emptyBin});
    if (!p.waitForFinished(30000))
        return -1;
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    if (out.contains(QStringLiteral("READY=1")))
        return 1;
    if (out.contains(QStringLiteral("READY=0")))
        return 0;
    return -1;
}

// Fixed timestamps rather than sleeps: the comparison is between two
// mtimes, and asking for them a second apart is the whole point.
bool writeStamped(const QString& path, const QString& date)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write("x");
    f.close();
    QProcess touch;
    touch.start(QStringLiteral("touch"),
                {QStringLiteral("-d"), date, path});
    return touch.waitForFinished(10000) && touch.exitCode() == 0;
}

} // namespace

TEST_CASE("A machine with no time synchronisation service is trusted",
          "[platformquirks][netready]")
{
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString net = tmp.filePath(QStringLiteral("net"));
    const QString lib = tmp.filePath(QStringLiteral("lib-systemd"));
    const QString var = tmp.filePath(QStringLiteral("var-lib-systemd"));
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(lib));
    REQUIRE(QDir().mkpath(var + QStringLiteral("/timesync")));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(buildOnlineNetFixture(net));

    // A clock file that would read as stale if anything looked at it. It is
    // here so this case cannot pass by accident: were the /lib/systemd bind
    // to fail, the probe would find the host's real systemd-timesyncd, look
    // at this file, and answer 0.
    REQUIRE(writeStamped(var + QStringLiteral("/timesync/clock"),
                         QStringLiteral("2001-01-01 00:00:00")));

    // No systemd-timesyncd installed at all. Waiting for a service that will
    // never run would leave the OS list empty for good, so its absence means
    // the clock is whatever the machine says it is and the fetch goes ahead.
    CHECK(networkReadyWith(net, lib, var, bin) == 1);
}

TEST_CASE("Time not yet synchronised holds the fetch back",
          "[platformquirks][netready]")
{
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString net = tmp.filePath(QStringLiteral("net"));
    const QString lib = tmp.filePath(QStringLiteral("lib-systemd"));
    const QString var = tmp.filePath(QStringLiteral("var-lib-systemd"));
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(lib));
    REQUIRE(QDir().mkpath(var + QStringLiteral("/timesync")));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(buildOnlineNetFixture(net));
    REQUIRE(writeStamped(lib + QStringLiteral("/systemd-timesyncd"),
                         QStringLiteral("2025-01-01 00:00:00")));

    SECTION("because the service has not written its clock file yet")
    {
        // First boot with no network yet: timesyncd is installed but has
        // never had an answer. Fetching now would hit a certificate that is
        // not valid until years from the clock's point of view.
        //
        // The explicit "clock file does not exist" guard is defended twice:
        // removing it leaves the mtime comparison reading a missing file,
        // whose lastModified() is an invalid QDateTime and loses to any real
        // one. So this assertion holds with the guard gone. It is here for
        // the behaviour, not as a check on that line.
        CHECK(networkReadyWith(net, lib, var, bin) == 0);
    }

    SECTION("because the clock file predates the service that writes it")
    {
        // A clock file left over from before the package was updated says
        // nothing about this boot.
        REQUIRE(writeStamped(var + QStringLiteral("/timesync/clock"),
                             QStringLiteral("2024-06-01 00:00:00")));
        CHECK(networkReadyWith(net, lib, var, bin) == 0);
    }
}

TEST_CASE("Once the clock has been set the OS list is fetched",
          "[platformquirks][netready]")
{
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString net = tmp.filePath(QStringLiteral("net"));
    const QString lib = tmp.filePath(QStringLiteral("lib-systemd"));
    const QString var = tmp.filePath(QStringLiteral("var-lib-systemd"));
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(lib));
    REQUIRE(QDir().mkpath(var + QStringLiteral("/timesync")));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(buildOnlineNetFixture(net));
    REQUIRE(writeStamped(lib + QStringLiteral("/systemd-timesyncd"),
                         QStringLiteral("2025-01-01 00:00:00")));
    REQUIRE(writeStamped(var + QStringLiteral("/timesync/clock"),
                         QStringLiteral("2026-09-08 12:00:00")));

    CHECK(networkReadyWith(net, lib, var, bin) == 1);
}

TEST_CASE("A synchronised clock on a machine with no network is still not ready",
          "[platformquirks][netready]")
{
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString net = tmp.filePath(QStringLiteral("net"));
    const QString lib = tmp.filePath(QStringLiteral("lib-systemd"));
    const QString var = tmp.filePath(QStringLiteral("var-lib-systemd"));
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(lib));
    REQUIRE(QDir().mkpath(var + QStringLiteral("/timesync")));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(writeStamped(lib + QStringLiteral("/systemd-timesyncd"),
                         QStringLiteral("2025-01-01 00:00:00")));
    REQUIRE(writeStamped(var + QStringLiteral("/timesync/clock"),
                         QStringLiteral("2026-09-08 12:00:00")));

    // Every interface down. The clock is fine, but there is nothing to fetch
    // over -- the connectivity check comes first for a reason, and a "ready"
    // here would send the embedded UI into a fetch that cannot succeed.
    REQUIRE(buildNetFixture(net, {{QStringLiteral("eth0"), QStringLiteral("down")},
                                  {QStringLiteral("wlan0"), QStringLiteral("down")}}));

    CHECK(networkReadyWith(net, lib, var, bin) == 0);
}

// ══════════════════════════════════════════════════════════════
// Asking NetworkManager when sysfs says nothing is up.
//
// The interface walk above is the fast path. Where it finds nothing -- a
// machine whose connection is a VPN, a bridge, or anything else that does
// not report "up" in operstate -- Imager asks nmcli before deciding it is
// offline. That second question is what stands between such a machine and
// the offline screen, and the cases above deliberately make nmcli unfindable
// so it cannot answer for the real network, which left it untested.
//
// The probe's PATH is a directory of this test's own making either way; here
// it has an nmcli in it.
// ══════════════════════════════════════════════════════════════

namespace {

// A nmcli that prints `answer` and nothing else, and records that it ran.
bool plantNmcli(const QString& binDir, const QByteArray& body)
{
    const QString path = binDir + QStringLiteral("/nmcli");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray script = "#!/bin/sh\n" + body;
    if (f.write(script) != script.size())
        return false;
    f.close();
    return f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner);
}

// Every interface present but none of them up: the state that sends the
// question on to nmcli.
bool buildAllDownFixture(const QString& root)
{
    return buildNetFixture(root, {{QStringLiteral("eth0"), QStringLiteral("down")},
                                  {QStringLiteral("wlan0"), QStringLiteral("down")}});
}

} // namespace

TEST_CASE("NetworkManager is asked when no interface reports itself up",
          "[platformquirks][netdetect][nmcli]")
{
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString net = tmp.filePath(QStringLiteral("net"));
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(buildAllDownFixture(net));

    SECTION("full connectivity means the OS list can be fetched")
    {
        // A VPN or a bridge, where operstate never says "up" for the thing
        // actually carrying traffic. Without this question the user sits on
        // the offline screen with a working connection.
        REQUIRE(plantNmcli(bin, "echo full\n"));
        CHECK(connectivityWith(net, bin) == 1);
    }

    SECTION("limited connectivity is still worth trying")
    {
        REQUIRE(plantNmcli(bin, "echo limited\n"));
        CHECK(connectivityWith(net, bin) == 1);
    }

    SECTION("none means offline")
    {
        REQUIRE(plantNmcli(bin, "echo none\n"));
        CHECK(connectivityWith(net, bin) == 0);
    }

    SECTION("a captive portal is not connectivity")
    {
        // NetworkManager reports "portal" when something is intercepting.
        // Fetching then returns the portal's login page rather than the OS
        // list, so the offline screen -- which offers Retry -- is the more
        // useful answer.
        REQUIRE(plantNmcli(bin, "echo portal\n"));
        CHECK(connectivityWith(net, bin) == 0);
    }

    SECTION("an answer nobody recognises is not taken as yes")
    {
        REQUIRE(plantNmcli(bin, "echo something-new\n"));
        CHECK(connectivityWith(net, bin) == 0);
    }

    SECTION("and nor is nmcli failing")
    {
        REQUIRE(plantNmcli(bin, "echo 'Error: NetworkManager is not running.' >&2\nexit 8\n"));
        CHECK(connectivityWith(net, bin) == 0);
    }
}

TEST_CASE("An nmcli that does not answer does not hold anything up",
          "[platformquirks][netdetect][nmcli]")
{
    // This runs on a one-second poll while the offline screen is showing, and
    // on the GUI thread's behalf. A NetworkManager wedged on a D-Bus call
    // would otherwise take the window with it.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString net = tmp.filePath(QStringLiteral("net"));
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(buildAllDownFixture(net));
    // Absolute: the probe's PATH is the fixture directory alone, so a bare
    // "sleep" would not be found and the script would fall straight through
    // to the echo -- answering instantly, which is the opposite of the case.
    REQUIRE(plantNmcli(bin, "/bin/sleep 30\necho full\n"));

    QElapsedTimer elapsed;
    elapsed.start();
    const int answer = connectivityWith(net, bin);
    const qint64 took = elapsed.elapsed();

    INFO("took " << took << " ms");
    CHECK(answer == 0);
    // Well inside the thirty seconds the fixture would take, so this is the
    // timeout firing rather than the sleep finishing.
    CHECK(took < 15000);
}

TEST_CASE("An interface that is up settles it without spawning anything",
          "[platformquirks][netdetect][nmcli]")
{
    // The sysfs walk is first because it is a couple of file reads; nmcli is
    // a process spawn, and this is called on a timer. If the order ever
    // flipped, every tick would fork.
    if (!haveMountNamespaces())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString net = tmp.filePath(QStringLiteral("net"));
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(buildNetFixture(net, {{QStringLiteral("eth0"), QStringLiteral("up")}}));

    // Says the opposite of the fixture, and leaves a mark if it is consulted.
    const QString marker = tmp.filePath(QStringLiteral("nmcli-ran"));
    REQUIRE(plantNmcli(bin, "touch \"" + marker.toUtf8() + "\"\necho none\n"));

    CHECK(connectivityWith(net, bin) == 1);
    CHECK_FALSE(QFileInfo::exists(marker));
}

#ifdef BEEP_PROBE_BINARY
// ══════════════════════════════════════════════════════════════
// The chime at the end of a write.
//
// How a user who has looked away, or whose window is behind something else,
// learns the write has finished -- and for a long write on a slow card, that
// is a real wait. isBeepAvailable() decides whether "beep when finished" is
// offered at all; offering it where nothing can make a sound is a setting
// that silently does nothing.
//
// beep() then works down a list. The interesting part is not the order but
// the falling through: a tool being installed is not the same as it working,
// and the commonest case -- canberra-gtk-play present but the sound theme
// missing -- has to move on to the next rather than give up.
//
// Both cache what they find for the life of the process, so every case is a
// fresh one with PATH pointed at a directory of fakes.
// ══════════════════════════════════════════════════════════════

namespace {

// A fake audio tool: records that it ran, and exits with `code`.
bool plantAudioTool(const QString& binDir, const QString& name,
                    const QString& markerDir, int code)
{
    const QString path = binDir + QLatin1Char('/') + name;
    // `: >` is a shell builtin. PATH here is the fixture directory alone, so
    // touch would not be found and the mark would never appear -- the case
    // would then read as "this tool was not tried", which is what it is
    // meant to be distinguishing.
    const QByteArray script =
        "#!/bin/sh\n"
        ": > \"" + markerDir.toUtf8() + "/" + name.toUtf8() + "\"\n"
        "exit " + QByteArray::number(code) + "\n";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (f.write(script) != script.size())
        return false;
    f.close();
    return f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner);
}

QString runBeepProbe(const QString& binDir, const QString& mode)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Pointed at the fixture rather than unset: QStandardPaths::
    // findExecutable falls back to a built-in default when PATH is empty and
    // would find the machine's real audio tools.
    env.insert(QStringLiteral("PATH"), binDir);
    p.setProcessEnvironment(env);
    p.start(QStringLiteral(BEEP_PROBE_BINARY), QStringList{mode});
    if (!p.waitForFinished(30000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

// findSoundFile() looks in /usr/share/sounds, which is not redirectable
// without a mount namespace. The cases that need one say so rather than
// asserting something that depends on the host.
bool haveASystemSoundFile()
{
    for (const char* path : {"/usr/share/sounds/freedesktop/stereo/complete.oga",
                             "/usr/share/sounds/freedesktop/stereo/bell.oga",
                             "/usr/share/sounds/Yaru/stereo/complete.oga",
                             "/usr/share/sounds/ocean/stereo/completion.oga",
                             "/usr/share/sounds/gnome/default/alerts/glass.ogg"}) {
        if (QFileInfo::exists(QString::fromLatin1(path)))
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("With nothing installed to make a sound, the option is not offered",
          "[platformquirks][beep]")
{
    // A minimal or headless install. Offering "beep when finished" here
    // gives the user a switch that does nothing at all.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(bin));

    CHECK(runBeepProbe(bin, QStringLiteral("available"))
              .contains(QStringLiteral("AVAILABLE=0")));
}

TEST_CASE("Any one of the mechanisms is enough to offer it",
          "[platformquirks][beep]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    const QString marks = tmp.filePath(QStringLiteral("marks"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(QDir().mkpath(marks));

    SECTION("canberra-gtk-play, which needs no sound file of its own")
    {
        // It plays from the desktop's sound theme, so it is offered even
        // where none of the file paths exist.
        REQUIRE(plantAudioTool(bin, QStringLiteral("canberra-gtk-play"), marks, 0));
        CHECK(runBeepProbe(bin, QStringLiteral("available"))
                  .contains(QStringLiteral("AVAILABLE=1")));
    }

    SECTION("the PC speaker, which needs nothing at all")
    {
        REQUIRE(plantAudioTool(bin, QStringLiteral("beep"), marks, 0));
        CHECK(runBeepProbe(bin, QStringLiteral("available"))
                  .contains(QStringLiteral("AVAILABLE=1")));
    }

    SECTION("a player, given there is something for it to play")
    {
        if (!haveASystemSoundFile())
            SKIP("this machine has none of the sound files the players need");
        REQUIRE(plantAudioTool(bin, QStringLiteral("pw-play"), marks, 0));
        CHECK(runBeepProbe(bin, QStringLiteral("available"))
                  .contains(QStringLiteral("AVAILABLE=1")));
    }
}

TEST_CASE("The desktop's own sound theme is tried first",
          "[platformquirks][beep]")
{
    // canberra-gtk-play plays the theme's "complete" sound, which is what
    // the user hears from everything else on their desktop. Reaching for a
    // file directly first would play a different noise from the rest of the
    // system.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    const QString marks = tmp.filePath(QStringLiteral("marks"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(QDir().mkpath(marks));

    REQUIRE(plantAudioTool(bin, QStringLiteral("canberra-gtk-play"), marks, 0));
    REQUIRE(plantAudioTool(bin, QStringLiteral("pw-play"), marks, 0));
    REQUIRE(plantAudioTool(bin, QStringLiteral("aplay"), marks, 0));

    REQUIRE(runBeepProbe(bin, QStringLiteral("beep")).contains(QStringLiteral("DONE=1")));

    CHECK(QFileInfo::exists(marks + QStringLiteral("/canberra-gtk-play")));
    CHECK_FALSE(QFileInfo::exists(marks + QStringLiteral("/pw-play")));
    CHECK_FALSE(QFileInfo::exists(marks + QStringLiteral("/aplay")));
}

TEST_CASE("A player that is installed but fails is not the end of it",
          "[platformquirks][beep]")
{
    // The case that actually happens: canberra-gtk-play installed as a
    // dependency of something else, with no sound theme behind it, so it
    // exits non-zero. Stopping there would mean no chime on a machine that
    // has three other ways to make one.
    if (!haveASystemSoundFile())
        SKIP("this machine has none of the sound files the later players need");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    const QString marks = tmp.filePath(QStringLiteral("marks"));
    REQUIRE(QDir().mkpath(bin));
    REQUIRE(QDir().mkpath(marks));

    SECTION("the next one along is tried")
    {
        REQUIRE(plantAudioTool(bin, QStringLiteral("canberra-gtk-play"), marks, 1));
        REQUIRE(plantAudioTool(bin, QStringLiteral("pw-play"), marks, 0));

        REQUIRE(runBeepProbe(bin, QStringLiteral("beep")).contains(QStringLiteral("DONE=1")));

        CHECK(QFileInfo::exists(marks + QStringLiteral("/canberra-gtk-play")));
        CHECK(QFileInfo::exists(marks + QStringLiteral("/pw-play")));
    }

    SECTION("and the one after that, all the way down")
    {
        REQUIRE(plantAudioTool(bin, QStringLiteral("canberra-gtk-play"), marks, 1));
        REQUIRE(plantAudioTool(bin, QStringLiteral("pw-play"), marks, 1));
        REQUIRE(plantAudioTool(bin, QStringLiteral("aplay"), marks, 1));
        REQUIRE(plantAudioTool(bin, QStringLiteral("pactl"), marks, 1));
        REQUIRE(plantAudioTool(bin, QStringLiteral("beep"), marks, 0));

        REQUIRE(runBeepProbe(bin, QStringLiteral("beep")).contains(QStringLiteral("DONE=1")));

        CHECK(QFileInfo::exists(marks + QStringLiteral("/pw-play")));
        CHECK(QFileInfo::exists(marks + QStringLiteral("/aplay")));
        CHECK(QFileInfo::exists(marks + QStringLiteral("/pactl")));
        CHECK(QFileInfo::exists(marks + QStringLiteral("/beep")));
    }
}

TEST_CASE("Asking for a chime where none can be made is not a crash",
          "[platformquirks][beep]")
{
    // The setting can be on from a machine that had the tools, or from a
    // synchronised profile. Nothing plays, and that is all that happens.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bin = tmp.filePath(QStringLiteral("bin"));
    REQUIRE(QDir().mkpath(bin));

    CHECK(runBeepProbe(bin, QStringLiteral("beep")).contains(QStringLiteral("DONE=1")));
}
#endif // BEEP_PROBE_BINARY

#endif // NETWORK_PROBE_BINARY
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
#ifdef ELEVATION_PROBE_BINARY
// ══════════════════════════════════════════════════════════════
// Whether Imager believes it can already elevate
//
// An AppImage needs a polkit policy authorising pkexec to run it as root
// before it can write to a disk. hasElevationPolicyInstalled() is what
// decides whether the application offers to install one -- so a wrong answer
// either way is visible: believing a policy is there when it is not sends
// the user to a pkexec prompt that refuses, and believing it is missing when
// it is there asks for a root password to install something that already
// exists.
//
// The scan reads every .policy file in /etc/polkit-1/actions and
// /usr/share/polkit-1/actions looking for one whose exec.path annotation
// names this binary. Both are absolute, so synthetic ones are bind-mounted
// over them and the probe run inside. Both are masked in every case, the
// real /usr/share/polkit-1/actions included, so the fixture is the only
// thing being read.
// ══════════════════════════════════════════════════════════════

namespace {

// A policy file granting pkexec the right to run `execPath`, in the shape the
// installer writes.
QByteArray policyGranting(const QString& execPath)
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<policyconfig>\n"
        "  <action id=\"com.raspberrypi.rpi-imager.pkexec.run\">\n"
        "    <message>Authentication is required to write to a disk</message>\n"
        "    <defaults><allow_any>auth_admin</allow_any></defaults>\n"
        "    <annotate key=\"org.freedesktop.policykit.exec.path\">%1</annotate>\n"
        "    <annotate key=\"org.freedesktop.policykit.exec.allow_gui\">true</annotate>\n"
        "  </action>\n"
        "</policyconfig>\n").arg(execPath).toUtf8();
}

bool writePolicyFile(const QString& dir, const QString& name, const QByteArray& body)
{
    if (!QDir().mkpath(dir))
        return false;
    QFile f(QDir(dir).filePath(name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(body) == body.size();
}

// The probe's own path, which is what the scan looks for.
QString probeBundlePath()
{
    QProcess p;
    p.start(QStringLiteral(ELEVATION_PROBE_BINARY), {QStringLiteral("policy")});
    if (!p.waitForFinished(30000))
        return {};
    for (const QString& line :
         QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("BUNDLE=")))
            return line.mid(QStringLiteral("BUNDLE=").size());
    }
    return {};
}

// Whether the probe reports a policy, with `etcRoot` over /etc/polkit-1 and
// `usrActions` over /usr/share/polkit-1/actions. -1 if it could not be run.
int policyInstalledWith(const QString& etcRoot, const QString& usrActions)
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral("mount --bind \"$1\" /etc/polkit-1 "
                            "&& mount --bind \"$2\" /usr/share/polkit-1/actions "
                            "&& exec \"$3\" policy"),
             QStringLiteral("_"), etcRoot, usrActions,
             QStringLiteral(ELEVATION_PROBE_BINARY)});
    if (!p.waitForFinished(30000))
        return -1;
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    if (out.contains(QStringLiteral("POLICY=1"))) return 1;
    if (out.contains(QStringLiteral("POLICY=0"))) return 0;
    return -1;
}

#define REQUIRE_POLICY_HARNESS()                                                    \
    if (!haveMountNamespacesForPolicy())                                            \
        SKIP("unprivileged mount namespaces are unavailable, so the polkit "        \
             "directories cannot be replaced")

bool haveMountNamespacesForPolicy()
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("true")});
    if (!p.waitForFinished(10000))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

} // namespace

TEST_CASE("A policy naming this binary is found", "[platformquirks][policy]")
{
    REQUIRE_POLICY_HARNESS();

    const QString bundle = probeBundlePath();
    REQUIRE_FALSE(bundle.isEmpty());

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    REQUIRE(writePolicyFile(etc.path() + QStringLiteral("/actions"),
                            QStringLiteral("com.raspberrypi.rpi-imager.appimage-abc.policy"),
                            policyGranting(bundle)));

    CHECK(policyInstalledWith(etc.path(), usr.path()) == 1);
}

TEST_CASE("The vendor directory is searched too", "[platformquirks][policy]")
{
    // /usr/share/polkit-1/actions is where a packaged install puts it, and
    // /etc is the override location for distributions where /usr is
    // read-only. Missing either would have Imager offer to install a policy
    // that is already there.
    REQUIRE_POLICY_HARNESS();

    const QString bundle = probeBundlePath();
    REQUIRE_FALSE(bundle.isEmpty());

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    REQUIRE(QDir().mkpath(etc.path() + QStringLiteral("/actions")));
    REQUIRE(writePolicyFile(usr.path(),
                            QStringLiteral("com.raspberrypi.rpi-imager.appimage-def.policy"),
                            policyGranting(bundle)));

    CHECK(policyInstalledWith(etc.path(), usr.path()) == 1);
}

TEST_CASE("Another AppImage's policy is not taken for ours",
          "[platformquirks][policy]")
{
    // The case the per-path hash exists for. A policy authorising a copy at
    // some other location grants this one nothing, so reading it as ours
    // would send the user to a pkexec prompt that refuses -- with the
    // application insisting it is already authorised.
    REQUIRE_POLICY_HARNESS();

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    REQUIRE(writePolicyFile(etc.path() + QStringLiteral("/actions"),
                            QStringLiteral("com.raspberrypi.rpi-imager.appimage-old.policy"),
                            policyGranting(QStringLiteral("/opt/elsewhere/rpi-imager.AppImage"))));

    CHECK(policyInstalledWith(etc.path(), usr.path()) == 0);
}

TEST_CASE("No policy anywhere is reported as none", "[platformquirks][policy]")
{
    REQUIRE_POLICY_HARNESS();

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    REQUIRE(QDir().mkpath(etc.path() + QStringLiteral("/actions")));

    CHECK(policyInstalledWith(etc.path(), usr.path()) == 0);
}

TEST_CASE("A file that is not a policy is not read as one",
          "[platformquirks][policy]")
{
    // Whatever else lives in those directories -- an editor's backup, a
    // half-written file, another project's policy -- must not be mistaken
    // for authorisation.
    REQUIRE_POLICY_HARNESS();

    const QString bundle = probeBundlePath();
    REQUIRE_FALSE(bundle.isEmpty());

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    const QString actions = etc.path() + QStringLiteral("/actions");

    // The path is in the file, but not as an exec.path annotation.
    REQUIRE(writePolicyFile(actions, QStringLiteral("notes.policy"),
                            QStringLiteral("<!-- reminder: authorise %1 one day -->\n")
                                .arg(bundle).toUtf8()));
    // And a file that is not scanned at all, whatever it says.
    REQUIRE(writePolicyFile(actions, QStringLiteral("stashed.policy.bak"),
                            policyGranting(bundle)));

    CHECK(policyInstalledWith(etc.path(), usr.path()) == 0);
}
#endif // ELEVATION_PROBE_BINARY
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
#ifdef ELEVATION_PROBE_BINARY
// ══════════════════════════════════════════════════════════════
// Installing the policy that grants root
//
// Writing the polkit policy is what makes an AppImage able to write to a
// disk at all, and it runs as root. Two properties matter more than whether
// it works: that it refuses when it is not root, and that clearing out old
// policies does not clear out anybody else's.
//
// A policy left behind for a binary that has since been deleted is a
// standing grant of root to whatever is put at that path next, which is why
// the installer sweeps them. But a policy for a binary that is still there
// belongs to a working install, and removing it would silently revoke that
// install's rights.
//
// unshare -r maps this user to root inside the namespace, so the installer
// runs for real against a synthetic /etc/polkit-1 -- and because a bind
// mount shares the filesystem underneath, what it wrote can be read back
// from the fixture afterwards.
// ══════════════════════════════════════════════════════════════

namespace {

QStringList policyFilesIn(const QString& dir)
{
    return QDir(dir).entryList(QStringList() << QStringLiteral("*.policy"),
                               QDir::Files, QDir::Name);
}

// Run one probe mode with `etcRoot` over /etc/polkit-1 and `usrActions` over
// /usr/share/polkit-1/actions. Returns its stdout, empty on failure.
// The same, with /etc/polkit-1 made read-only after it is bound. A read-only
// *mount* is what stops root: file permissions do not, since root ignores
// them, and root is exactly who installs a policy.
QString runProbeInReadOnlyPolkitNamespace(const QString& mode, const QString& etcRoot,
                                          const QString& usrActions)
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral("mount --bind \"$1\" /etc/polkit-1 "
                            "&& mount -o remount,bind,ro /etc/polkit-1 "
                            "&& mount --bind \"$2\" /usr/share/polkit-1/actions "
                            "&& mount -o remount,bind,ro /usr/share/polkit-1/actions "
                            "&& exec \"$3\" \"$4\""),
             QStringLiteral("_"), etcRoot, usrActions,
             QStringLiteral(ELEVATION_PROBE_BINARY), mode});
    if (!p.waitForFinished(30000))
        return {};
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString runProbeInPolkitNamespace(const QString& mode, const QString& etcRoot,
                                  const QString& usrActions)
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral("mount --bind \"$1\" /etc/polkit-1 "
                            "&& mount --bind \"$2\" /usr/share/polkit-1/actions "
                            "&& exec \"$3\" \"$4\""),
             QStringLiteral("_"), etcRoot, usrActions,
             QStringLiteral(ELEVATION_PROBE_BINARY), mode});
    if (!p.waitForFinished(30000))
        return {};
    return QString::fromUtf8(p.readAllStandardOutput());
}

} // namespace

TEST_CASE("A policy that cannot be written is reported, not assumed",
          "[platformquirks][policyinstall]")
{
    // Installing the policy is what lets an AppImage elevate itself to write
    // to a disk. If it cannot be written -- a read-only /etc, an immutable
    // filesystem, a container -- saying so is the whole of the difference
    // between the user seeing a failure now and finding out later that
    // writing a card does not work, with a polkit prompt that never appears.
    //
    // Read-only as a *mount*, not as permissions: root ignores permissions,
    // and root is who installs a policy.
    REQUIRE_POLICY_HARNESS();

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    const QString actions = etc.path() + QStringLiteral("/actions");
    REQUIRE(QDir().mkpath(actions));

    const QString out = runProbeInReadOnlyPolkitNamespace(QStringLiteral("install"),
                                                          etc.path(), usr.path());
    INFO(out.toStdString());
    REQUIRE_FALSE(out.isEmpty());
    CHECK(out.contains(QStringLiteral("INSTALLED=0")));

    // And nothing was left behind. A half-written policy in the actions
    // directory is worse than none: it is a file granting root that nobody
    // has finished writing.
    CHECK(policyFilesIn(actions).isEmpty());
}

TEST_CASE("A refused install does not report a policy that is not there",
          "[platformquirks][policyinstall]")
{
    // The check the application makes before offering to elevate. Having
    // just failed to install one, it must not then say one exists -- that
    // combination puts the user on a screen with no Install Authorization
    // button and no way to write a card either.
    REQUIRE_POLICY_HARNESS();

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    REQUIRE(QDir().mkpath(etc.path() + QStringLiteral("/actions")));

    runProbeInReadOnlyPolkitNamespace(QStringLiteral("install"), etc.path(), usr.path());

    const QString check = runProbeInPolkitNamespace(QStringLiteral("policy"),
                                                    etc.path(), usr.path());
    INFO(check.toStdString());
    CHECK(check.contains(QStringLiteral("POLICY=0")));
}

TEST_CASE("Installing a policy writes one naming this binary, and it is then found",
          "[platformquirks][policyinstall]")
{
    REQUIRE_POLICY_HARNESS();

    const QString bundle = probeBundlePath();
    REQUIRE_FALSE(bundle.isEmpty());

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    const QString actions = etc.path() + QStringLiteral("/actions");
    REQUIRE(QDir().mkpath(actions));
    REQUIRE(policyFilesIn(actions).isEmpty());

    const QString out = runProbeInPolkitNamespace(QStringLiteral("install"),
                                                  etc.path(), usr.path());
    INFO(out.toStdString());
    CHECK(out.contains(QStringLiteral("INSTALLED=1")));

    // One file, named the way the cleanup sweep recognises.
    const QStringList written = policyFilesIn(actions);
    REQUIRE(written.size() == 1);
    CHECK(written.first().startsWith(
        QStringLiteral("com.raspberrypi.rpi-imager.appimage-")));

    // And the round trip: what was written is what the check reads back.
    const QString check = runProbeInPolkitNamespace(QStringLiteral("policy"),
                                                    etc.path(), usr.path());
    INFO(check.toStdString());
    CHECK(check.contains(QStringLiteral("POLICY=1")));
}

TEST_CASE("Installing sweeps away a policy for a binary that is gone",
          "[platformquirks][policyinstall]")
{
    // A grant of root left pointing at a path with nothing at it. Whatever
    // is put there next inherits the right to run as root, so the sweep is
    // the point rather than tidiness.
    REQUIRE_POLICY_HARNESS();

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    const QString actions = etc.path() + QStringLiteral("/actions");
    REQUIRE(writePolicyFile(actions,
                            QStringLiteral("com.raspberrypi.rpi-imager.appimage-gone.policy"),
                            policyGranting(QStringLiteral("/opt/deleted-appimage/rpi-imager.AppImage"))));

    runProbeInPolkitNamespace(QStringLiteral("install"), etc.path(), usr.path());

    const QStringList left = policyFilesIn(actions);
    INFO(left.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(left.contains(
        QStringLiteral("com.raspberrypi.rpi-imager.appimage-gone.policy")));
}

TEST_CASE("Installing leaves alone a policy for a binary that is still there",
          "[platformquirks][policyinstall]")
{
    // Another copy of Imager, installed elsewhere and still present.
    // Removing its policy would revoke its rights the next time this one was
    // launched, and nothing would say why it had stopped being able to write.
    REQUIRE_POLICY_HARNESS();

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    const QString actions = etc.path() + QStringLiteral("/actions");
    // /bin/sh stands in for the other copy: a path that certainly exists.
    REQUIRE(writePolicyFile(actions,
                            QStringLiteral("com.raspberrypi.rpi-imager.appimage-other.policy"),
                            policyGranting(QStringLiteral("/bin/sh"))));

    runProbeInPolkitNamespace(QStringLiteral("install"), etc.path(), usr.path());

    const QStringList left = policyFilesIn(actions);
    INFO(left.join(QStringLiteral(", ")).toStdString());
    CHECK(left.contains(
        QStringLiteral("com.raspberrypi.rpi-imager.appimage-other.policy")));
}

TEST_CASE("Installing does not touch somebody else's policy",
          "[platformquirks][policyinstall]")
{
    // The sweep is limited to files named the way this application names
    // them. Everything else in that directory belongs to another package,
    // and deleting one -- as root, on the way to writing an image -- would
    // take away rights that have nothing to do with Imager.
    REQUIRE_POLICY_HARNESS();

    QTemporaryDir etc, usr;
    REQUIRE(etc.isValid());
    REQUIRE(usr.isValid());
    const QString actions = etc.path() + QStringLiteral("/actions");
    // Names another project's policy, for a path that does not exist -- so
    // it would be swept if the glob were any wider.
    REQUIRE(writePolicyFile(actions, QStringLiteral("org.example.tool.policy"),
                            policyGranting(QStringLiteral("/opt/gone/other-tool"))));

    runProbeInPolkitNamespace(QStringLiteral("install"), etc.path(), usr.path());

    const QStringList left = policyFilesIn(actions);
    INFO(left.join(QStringLiteral(", ")).toStdString());
    CHECK(left.contains(QStringLiteral("org.example.tool.policy")));
}

TEST_CASE("Nothing is installed when we are not root",
          "[platformquirks][policyinstall]")
{
    // The check that keeps this from being a way to write into
    // /etc/polkit-1/actions without permission. Run as an ordinary user it
    // has to refuse before touching anything.
    if (::geteuid() == 0)
        SKIP("running as root, so the refusal under test does not apply");

    QProcess p;
    p.start(QStringLiteral(ELEVATION_PROBE_BINARY), {QStringLiteral("install")});
    REQUIRE(p.waitForFinished(30000));

    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    INFO(out.toStdString());
    CHECK(out.contains(QStringLiteral("INSTALLED=0")));
}
#endif // ELEVATION_PROBE_BINARY
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
// ══════════════════════════════════════════════════════════════
// Which mounts belong to the card being written
//
// Imager unmounts the target before writing to it, and works out what to
// unmount by matching every line of /proc/mounts against the device path.
// Matching one line too many means unmounting a filesystem on somebody
// else's disk, out from under whatever was using it -- so this is the sort
// of comparison worth being exact about, and it had no tests.
//
// The kernel names a partition after its disk. Where the disk name ends in a
// letter the number is appended directly (sda1, and sda11 for the eleventh);
// where it ends in a digit the number is separated by 'p' (mmcblk0p1,
// nvme0n1p1, loop1p1). Only one of those forms can occur for any given disk.
// ══════════════════════════════════════════════════════════════

namespace PlatformQuirks::TestAPI {
bool mountIsOnDevice(const char* devicePath, const char* mountSource);
}

TEST_CASE("A disk and its own partitions are matched", "[platformquirks][unmount]")
{
    using PlatformQuirks::TestAPI::mountIsOnDevice;

    // Names ending in a letter: the number goes straight on.
    CHECK(mountIsOnDevice("/dev/sda", "/dev/sda"));
    CHECK(mountIsOnDevice("/dev/sda", "/dev/sda1"));
    CHECK(mountIsOnDevice("/dev/sda", "/dev/sda2"));
    CHECK(mountIsOnDevice("/dev/sda", "/dev/sda11"));

    // Names ending in a digit: separated by 'p'. These are the ones a user
    // of this application actually writes to.
    CHECK(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk0"));
    CHECK(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk0p1"));
    CHECK(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk0p2"));
    CHECK(mountIsOnDevice("/dev/nvme0n1", "/dev/nvme0n1p1"));
    CHECK(mountIsOnDevice("/dev/loop1", "/dev/loop1p1"));
}

TEST_CASE("A mount on another disk is left alone", "[platformquirks][unmount]")
{
    using PlatformQuirks::TestAPI::mountIsOnDevice;

    // Different disk entirely.
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "/dev/sdb"));
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "/dev/sdb1"));
    CHECK_FALSE(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk1p1"));

    // A longer name that merely starts the same way.
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "/dev/sda_backup"));
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "/dev/sdaa"));
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "/dev/sdaa1"));

    // The eMMC boot areas, which sit beside mmcblk0 and are not partitions
    // of it.
    CHECK_FALSE(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk0boot0"));
    CHECK_FALSE(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk0rpmb"));

    // Not a prefix at all.
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "/dev/nvme0n1p1"));
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "tmpfs"));
}

TEST_CASE("A device whose name ends in a digit does not swallow its neighbours",
          "[platformquirks][unmount]")
{
    using PlatformQuirks::TestAPI::mountIsOnDevice;

    // The case that made this worth extracting. Accepting a bare digit after
    // a name that already ends in one matched the *next* device along:
    // /dev/loop1 took in a mount on /dev/loop11, and unmounted it. Loop
    // devices are offered as write targets -- the drive list keeps them
    // deliberately, since a mounted disk image is a valid thing to write --
    // so this is reachable rather than theoretical.
    CHECK_FALSE(mountIsOnDevice("/dev/loop1", "/dev/loop11"));
    CHECK_FALSE(mountIsOnDevice("/dev/loop1", "/dev/loop11p1"));
    CHECK_FALSE(mountIsOnDevice("/dev/loop1", "/dev/loop12"));
    CHECK_FALSE(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk01"));
    CHECK_FALSE(mountIsOnDevice("/dev/nvme0n1", "/dev/nvme0n11"));

    // And the mirror image: a 'p' suffix on a name ending in a letter is not
    // a partition either. /dev/sdap1 is the first partition of the
    // forty-second SCSI disk, which a machine with a shelf of them has.
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "/dev/sdap1"));
    CHECK_FALSE(mountIsOnDevice("/dev/sdb", "/dev/sdbp2"));
}

TEST_CASE("A partial partition number is not a partition",
          "[platformquirks][unmount]")
{
    using PlatformQuirks::TestAPI::mountIsOnDevice;

    // 'p' with nothing after it, and digits with something after them.
    CHECK_FALSE(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk0p"));
    CHECK_FALSE(mountIsOnDevice("/dev/sda", "/dev/sda1x"));
    CHECK_FALSE(mountIsOnDevice("/dev/mmcblk0", "/dev/mmcblk0p1x"));
}

TEST_CASE("Nothing at all matches nothing", "[platformquirks][unmount]")
{
    using PlatformQuirks::TestAPI::mountIsOnDevice;

    CHECK_FALSE(mountIsOnDevice(nullptr, "/dev/sda1"));
    CHECK_FALSE(mountIsOnDevice("/dev/sda", nullptr));
    CHECK_FALSE(mountIsOnDevice("", "/dev/sda1"));
}
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
#ifdef ELEVATION_PROBE_BINARY
// ══════════════════════════════════════════════════════════════
// Finding the compositor when elevated under Wayland
//
// Running as root for somebody else, Imager has to be told where their
// display is or it cannot draw at all. Under Wayland that means finding the
// compositor's socket in the user's runtime directory, and the scan is not
// simply "the first thing called wayland-something": a lock file sits beside
// the socket with almost the same name, and the number is not always zero on
// a machine with a nested compositor or more than one seat.
//
// Pick the wrong one and the elevated process has a WAYLAND_DISPLAY pointing
// at nothing, which is a window that never appears.
//
// unshare -r is enough to reach this: it maps this user to root, which is
// the only thing the handover is gated on, so no sudo is needed. The runtime
// directory is bind-mounted, and /tmp/.X11-unix is masked empty -- with an
// X11 socket present the scan never runs, and Imager would go on to call
// xhost against the display of whoever is running the suite.
// ══════════════════════════════════════════════════════════════

namespace {

// A real AF_UNIX socket, since the scan checks the file type rather than the
// name alone.
bool makeUnixSocket(const QString& path)
{
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const QByteArray p = path.toUtf8();
    if (static_cast<size_t>(p.size()) >= sizeof(addr.sun_path)) {
        ::close(fd);
        return false;
    }
    std::memcpy(addr.sun_path, p.constData(), p.size());
    const bool ok = ::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

bool makePlainFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write("not a socket\n");
    return true;
}

// Run the probe as namespace-root with `runtimeDir` over the invoking user's
// /run/user/<uid> and an empty directory over /tmp/.X11-unix.
QString runProbeUnderWayland(const QString& runtimeDir, const QString& emptyX11)
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral(
                 "mount --bind \"$1\" \"/run/user/$4\" "
                 "&& mount --bind \"$2\" /tmp/.X11-unix "
                 "&& exec env -u DISPLAY -u WAYLAND_DISPLAY -u PKEXEC_UID "
                 "SUDO_UID=\"$4\" \"$3\""),
             QStringLiteral("_"), runtimeDir, emptyX11,
             QStringLiteral(ELEVATION_PROBE_BINARY),
             QString::number(::getuid())});
    if (!p.waitForFinished(30000))
        return {};
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString reportedValue(const QString& out, const QString& key)
{
    for (const QString& line : out.split(QLatin1Char('\n'))) {
        if (line.startsWith(key + QLatin1Char('=')))
            return line.mid(key.size() + 1);
    }
    return {};
}

} // namespace

TEST_CASE("The compositor socket is found, whatever it is numbered",
          "[platformquirks][wayland]")
{
    if (!haveMountNamespacesForPolicy())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir runtime, x11;
    REQUIRE(runtime.isValid());
    REQUIRE(x11.isValid());

    // Not wayland-0: a nested compositor or a second seat numbers them
    // higher, and the scan is meant to find whichever is there.
    REQUIRE(makeUnixSocket(QDir(runtime.path()).filePath(QStringLiteral("wayland-3"))));
    // The lock file that sits beside every one of them.
    REQUIRE(makePlainFile(QDir(runtime.path()).filePath(QStringLiteral("wayland-3.lock"))));
    // Something named like a socket that is not one.
    REQUIRE(makePlainFile(QDir(runtime.path()).filePath(QStringLiteral("wayland-9"))));

    const QString out = runProbeUnderWayland(runtime.path(), x11.path());
    INFO(out.toStdString());

    CHECK(reportedValue(out, QStringLiteral("AFTER_WAYLAND_DISPLAY"))
          == QStringLiteral("wayland-3"));
    // And no X11 display was invented, which is what keeps xhost out of it.
    CHECK(reportedValue(out, QStringLiteral("AFTER_DISPLAY")).isEmpty());
}

TEST_CASE("A lock file on its own is not a compositor",
          "[platformquirks][wayland]")
{
    // What is left behind when a compositor exits badly. Pointing
    // WAYLAND_DISPLAY at it gives a window that never appears, which is
    // worse than leaving it unset and letting Qt say so.
    if (!haveMountNamespacesForPolicy())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir runtime, x11;
    REQUIRE(runtime.isValid());
    REQUIRE(x11.isValid());
    REQUIRE(makePlainFile(QDir(runtime.path()).filePath(QStringLiteral("wayland-0.lock"))));

    const QString out = runProbeUnderWayland(runtime.path(), x11.path());
    INFO(out.toStdString());

    CHECK(reportedValue(out, QStringLiteral("AFTER_WAYLAND_DISPLAY")).isEmpty());
}

TEST_CASE("An empty runtime directory leaves the display alone",
          "[platformquirks][wayland]")
{
    // Nothing to find. The variable stays unset rather than being given a
    // guess, and the handover still does the rest of its work.
    if (!haveMountNamespacesForPolicy())
        SKIP("unprivileged mount namespaces are unavailable");

    QTemporaryDir runtime, x11;
    REQUIRE(runtime.isValid());
    REQUIRE(x11.isValid());

    const QString out = runProbeUnderWayland(runtime.path(), x11.path());
    INFO(out.toStdString());

    CHECK(reportedValue(out, QStringLiteral("AFTER_WAYLAND_DISPLAY")).isEmpty());
    // The handover itself still happened.
    CHECK(reportedValue(out, QStringLiteral("AFTER_HOME")) == QDir::homePath());
}
#endif // ELEVATION_PROBE_BINARY
#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
// Both sections below are Linux, and only Linux: pkexec elevation and
// launching a browser as the invoking user are implemented in
// platformquirks_linux.cpp alone, and so are the TestAPI entry points they
// call. They were written after the guard above closed, so they compiled
// everywhere and platformquirks_test stopped linking on macOS against
// buildElevationCommand and resolveOriginalUid.

// ══════════════════════════════════════════════════════════════
// The command line handed to pkexec.
//
// This is the one place in the application where a command line is built to
// be run as root, so it is worth being exact about. tryElevate re-runs the
// application under pkexec when the user asked for something that needs
// privileges; whatever ends up in this list runs with root's authority.
//
// The construction was inline in tryElevate, which forks and execs and so
// cannot be called from a test at all. Extracted so it can be.
// ══════════════════════════════════════════════════════════════

namespace PlatformQuirks::TestAPI {
QStringList buildElevationCommand(const QString& bundlePath,
                                  const QStringList& userArgs);
}

TEST_CASE("The program run as root is the resolved bundle path",
          "[platformquirks][elevate]")
{
    using PlatformQuirks::TestAPI::buildElevationCommand;

    // argv[0] is whatever the caller chose to put there -- a symlink name, a
    // relative path, or something crafted. The program pkexec is asked to run
    // is the path this application resolved for itself, and the user's
    // arguments start at argv[1]. Nothing the caller supplies can become the
    // program.
    const QStringList cmd = buildElevationCommand(
        QStringLiteral("/usr/bin/rpi-imager"),
        QStringList{QStringLiteral("--repo"), QStringLiteral("https://example.invalid/os.json")});

    REQUIRE(cmd.size() == 5);
    CHECK(cmd[0] == QStringLiteral("/usr/bin/pkexec"));
    CHECK(cmd[2] == QStringLiteral("/usr/bin/rpi-imager"));

    // The bundle path is at the program position, not merely present
    // somewhere: pkexec runs cmd[2] once its own options are consumed.
    const int programIndex = cmd.indexOf(QStringLiteral("/usr/bin/rpi-imager"));
    CHECK(programIndex == 2);
}

TEST_CASE("The desktop's own authentication dialog is the one shown",
          "[platformquirks][elevate]")
{
    using PlatformQuirks::TestAPI::buildElevationCommand;

    // Without --disable-internal-agent, pkexec starts its own text-mode
    // polkit agent. In a desktop session that fights the session's agent:
    // the user sees two prompts, or a prompt that never appears because it
    // was written to a terminal nobody is looking at. The flag has to come
    // before the program, or pkexec passes it to the program instead.
    const QStringList cmd = buildElevationCommand(
        QStringLiteral("/usr/bin/rpi-imager"), QStringList{});

    const int flagIndex = cmd.indexOf(QStringLiteral("--disable-internal-agent"));
    REQUIRE(flagIndex != -1);
    CHECK(flagIndex < cmd.indexOf(QStringLiteral("/usr/bin/rpi-imager")));
}

TEST_CASE("The user's arguments survive elevation intact",
          "[platformquirks][elevate]")
{
    using PlatformQuirks::TestAPI::buildElevationCommand;

    SECTION("in the order they were given")
    {
        const QStringList given{
            QStringLiteral("--cli"),
            QStringLiteral("/home/pi/My Images/2026-01-01-raspios.img.xz"),
            QStringLiteral("/dev/sda"),
        };
        const QStringList cmd = buildElevationCommand(
            QStringLiteral("/usr/bin/rpi-imager"), given);

        REQUIRE(cmd.size() == 3 + given.size());
        CHECK(cmd.mid(3) == given);
    }

    SECTION("an argument containing spaces stays one argument")
    {
        // The whole reason for building a list rather than a string. A path
        // with a space in it, joined and re-split, becomes two arguments and
        // the write targets a file that does not exist -- or, worse, the
        // wrong one.
        const QString path =
            QStringLiteral("/home/pi/Raspberry Pi/os images/raspios.img");
        const QStringList cmd = buildElevationCommand(
            QStringLiteral("/usr/bin/rpi-imager"), QStringList{path});

        REQUIRE(cmd.size() == 4);
        CHECK(cmd[3] == path);
    }

    SECTION("and so does one that looks like a shell fragment")
    {
        // execv takes the list as it stands: there is no shell to interpret
        // these, and nothing here is quoted or escaped away either.
        const QStringList given{
            QStringLiteral("--repo"),
            QStringLiteral("http://example.invalid/a;rm -rf /"),
            QStringLiteral("$(id)"),
            QStringLiteral("`id`"),
            QStringLiteral("a\nb"),
        };
        const QStringList cmd = buildElevationCommand(
            QStringLiteral("/usr/bin/rpi-imager"), given);

        REQUIRE(cmd.size() == 3 + given.size());
        CHECK(cmd.mid(3) == given);
    }

    SECTION("with no arguments, only the three elements are present")
    {
        const QStringList cmd = buildElevationCommand(
            QStringLiteral("/usr/bin/rpi-imager"), QStringList{});

        CHECK(cmd == QStringList{QStringLiteral("/usr/bin/pkexec"),
                                 QStringLiteral("--disable-internal-agent"),
                                 QStringLiteral("/usr/bin/rpi-imager")});
    }
}

TEST_CASE("An unusual install location is passed through as it is",
          "[platformquirks][elevate]")
{
    using PlatformQuirks::TestAPI::buildElevationCommand;

    // AppImage extracts to a temporary directory whose name changes every
    // run, and a locally built binary can be anywhere. The policy file is
    // matched against this same resolved path by hasPolkitPolicyForPath
    // before we get here, so what is checked and what is run agree.
    const QString bundle =
        QStringLiteral("/tmp/.mount_rpi-imAbC123/usr/bin/rpi-imager");
    const QStringList cmd = buildElevationCommand(bundle, QStringList{});

    REQUIRE(cmd.size() == 3);
    CHECK(cmd[2] == bundle);
}

// ══════════════════════════════════════════════════════════════
// Opening a link while the application is running as root.
//
// Every "read more" and licence link in the wizard goes through here. When
// the GUI is elevated, root's session bus has no portal and root's DISPLAY
// belongs to nobody, so the browser has to be launched as the user who
// started us. Get it wrong and the link silently does nothing -- the user
// clicks, and no window appears.
//
// Both halves were inside a function that forks a detached process, so
// neither had a test. Extracted: which user, and what command line.
// ══════════════════════════════════════════════════════════════

namespace PlatformQuirks::TestAPI {
unsigned int resolveOriginalUid(const char* pkexecUid, const char* sudoUid,
                                unsigned int realUid, unsigned int effectiveUid);
QStringList buildOpenUrlAsUserArgs(const QString& username,
                                   const QProcessEnvironment& env,
                                   const QString& url,
                                   const QString& userHome);
}

TEST_CASE("The user behind an elevated session is recognised",
          "[platformquirks][openurl]")
{
    using PlatformQuirks::TestAPI::resolveOriginalUid;

    // Elevated through the application's own pkexec path.
    CHECK(resolveOriginalUid("1000", nullptr, 0, 0) == 1000u);

    // Started with sudo from a terminal.
    CHECK(resolveOriginalUid(nullptr, "1000", 0, 0) == 1000u);

    // pkexec wins where both are set, which happens when a sudo shell
    // launches something that elevates again: PKEXEC_UID names the user of
    // the session actually in front of the machine.
    CHECK(resolveOriginalUid("1000", "1001", 0, 0) == 1000u);

    // A setuid-root binary: the real uid is still the user's.
    CHECK(resolveOriginalUid(nullptr, nullptr, 1000, 0) == 1000u);
}

TEST_CASE("With no invoking user to find, no link is opened on a guess",
          "[platformquirks][openurl]")
{
    using PlatformQuirks::TestAPI::resolveOriginalUid;

    // A root login. There is no other session; the caller warns and gives up
    // rather than picking a user.
    CHECK(resolveOriginalUid(nullptr, nullptr, 0, 0) == 0u);

    // Running as an ordinary user with no elevation at all -- reached only if
    // the euid check upstream let it through, and still not a reason to
    // launch a browser as somebody else.
    CHECK(resolveOriginalUid(nullptr, nullptr, 1000, 1000) == 0u);

    // PKEXEC_UID present but not a number. This stops the search rather than
    // falling through to SUDO_UID: an environment that is not what we think
    // it is should not have a second variable consulted, which could name a
    // different user's session.
    CHECK(resolveOriginalUid("", "1000", 0, 0) == 0u);
    CHECK(resolveOriginalUid("not-a-uid", "1000", 0, 0) == 0u);
}

// A plausible root environment for an elevated GUI: the session variables
// that matter, alongside plenty that must not be carried across.
static QProcessEnvironment elevatedRootEnvironment()
{
    QProcessEnvironment env;
    env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
               QStringLiteral("unix:path=/run/user/1000/bus"));
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), QStringLiteral("/run/user/1000"));
    env.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
    env.insert(QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("wayland-0"));
    env.insert(QStringLiteral("XAUTHORITY"), QStringLiteral("/home/pi/.Xauthority"));
    env.insert(QStringLiteral("HOME"), QStringLiteral("/root"));
    env.insert(QStringLiteral("USER"), QStringLiteral("root"));
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/sbin:/usr/bin"));
    env.insert(QStringLiteral("SSH_AUTH_SOCK"), QStringLiteral("/run/root-agent.sock"));
    return env;
}

TEST_CASE("The browser is launched on the user's own session",
          "[platformquirks][openurl]")
{
    using PlatformQuirks::TestAPI::buildOpenUrlAsUserArgs;

    const QString url =
        QStringLiteral("https://www.raspberrypi.com/documentation/computers/getting-started.html");
    const QStringList args = buildOpenUrlAsUserArgs(
        QStringLiteral("pi"), elevatedRootEnvironment(), url,
        QStringLiteral("/home/pi"));

    // runuser -u pi -- env ... xdg-open <url>
    REQUIRE(args.size() >= 6);
    CHECK(args[0] == QStringLiteral("-u"));
    CHECK(args[1] == QStringLiteral("pi"));

    // The -- keeps a username beginning with a dash, or anything later in the
    // list, from being read as an option to runuser.
    CHECK(args[2] == QStringLiteral("--"));
    CHECK(args[3] == QStringLiteral("env"));

    CHECK(args[args.size() - 2] == QStringLiteral("xdg-open"));
    CHECK(args.last() == url);
}

TEST_CASE("Only the session variables cross to the browser",
          "[platformquirks][openurl]")
{
    using PlatformQuirks::TestAPI::buildOpenUrlAsUserArgs;

    const QStringList args = buildOpenUrlAsUserArgs(
        QStringLiteral("pi"), elevatedRootEnvironment(),
        QStringLiteral("https://example.invalid/"), QStringLiteral("/home/pi"));

    // The five xdg-open needs to find the session.
    CHECK(args.contains(QStringLiteral("XDG_RUNTIME_DIR=/run/user/1000")));
    CHECK(args.contains(QStringLiteral("DISPLAY=:0")));
    CHECK(args.contains(QStringLiteral("WAYLAND_DISPLAY=wayland-0")));
    CHECK(args.contains(QStringLiteral("XAUTHORITY=/home/pi/.Xauthority")));

    // The bus address itself contains an '=' -- it is always of the form
    // unix:path=/run/user/N/bus. Splitting the assignment on the first '='
    // is what env does, so the value has to arrive whole.
    CHECK(args.contains(
        QStringLiteral("DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus")));

    // Root's own environment is not handed to a browser: HOME=/root would
    // have it read and write root's profile, and the rest is nobody's
    // business.
    for (const QString& unwanted : {QStringLiteral("HOME"), QStringLiteral("USER"),
                                    QStringLiteral("PATH"), QStringLiteral("SSH_AUTH_SOCK")}) {
        const QString prefix = unwanted + QLatin1Char('=');
        for (const QString& arg : args)
            CHECK_FALSE(arg.startsWith(prefix));
    }
}

TEST_CASE("A session variable that is not set is left out",
          "[platformquirks][openurl]")
{
    using PlatformQuirks::TestAPI::buildOpenUrlAsUserArgs;

    // A Wayland session has no DISPLAY or XAUTHORITY; an X11 one has no
    // WAYLAND_DISPLAY. Passing the missing ones as empty assignments would
    // tell xdg-open a display exists when it does not, and it would try to
    // reach it instead of the one that works.
    QProcessEnvironment wayland;
    wayland.insert(QStringLiteral("XDG_RUNTIME_DIR"), QStringLiteral("/run/user/1000"));
    wayland.insert(QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("wayland-0"));
    wayland.insert(QStringLiteral("DISPLAY"), QString());

    const QStringList args = buildOpenUrlAsUserArgs(
        QStringLiteral("pi"), wayland, QStringLiteral("https://example.invalid/"),
        QStringLiteral("/home/pi"));

    CHECK(args.contains(QStringLiteral("WAYLAND_DISPLAY=wayland-0")));
    for (const QString& arg : args) {
        CHECK_FALSE(arg.startsWith(QStringLiteral("DISPLAY=")));
        CHECK_FALSE(arg.startsWith(QStringLiteral("XAUTHORITY=")));
        CHECK_FALSE(arg.startsWith(QStringLiteral("DBUS_SESSION_BUS_ADDRESS=")));
    }

    // With none of them set at all, the command is still well formed: the
    // browser may not find the session, but runuser is not handed a
    // half-written line.
    //
    // XDG_DATA_DIRS is the exception and is always present, because it is
    // rebuilt rather than carried -- see the cases below.
    const QStringList bare = buildOpenUrlAsUserArgs(
        QStringLiteral("pi"), QProcessEnvironment(),
        QStringLiteral("https://example.invalid/"), QStringLiteral("/home/pi"));
    QStringList withoutDataDirs;
    for (const QString& arg : bare) {
        if (!arg.startsWith(QStringLiteral("XDG_DATA_DIRS=")))
            withoutDataDirs << arg;
    }
    CHECK(withoutDataDirs == QStringList{QStringLiteral("-u"), QStringLiteral("pi"),
                                         QStringLiteral("--"), QStringLiteral("env"),
                                         QStringLiteral("xdg-open"),
                                         QStringLiteral("https://example.invalid/")});
}

TEST_CASE("The browser is told where to look for itself",
          "[platformquirks][openurl][datadirs]")
{
    using PlatformQuirks::TestAPI::buildOpenUrlAsUserArgs;

    // pkexec replaces the environment with "a minimal known and safe" one and
    // XDG_DATA_DIRS is not in it, so by the time Imager is elevated the value
    // is gone; runuser does not put it back either. xdg-open then falls back
    // to /usr/local/share:/usr/share, which finds a browser installed as a
    // distribution package and does not find one installed as a Snap or a
    // Flatpak. Firefox is a Snap on a stock Ubuntu.
    //
    // The user's own mimeapps.list is found -- runuser restores HOME -- so
    // xdg-open knows the right browser by name and then cannot locate the
    // .desktop file that name refers to. The link does nothing at all.

    auto dataDirsIn = [](const QStringList& args) {
        for (const QString& arg : args) {
            if (arg.startsWith(QStringLiteral("XDG_DATA_DIRS=")))
                return arg.mid(QStringLiteral("XDG_DATA_DIRS=").size());
        }
        return QString();
    };

    SECTION("rebuilt when there is nothing left to carry")
    {
        const QStringList args = buildOpenUrlAsUserArgs(
            QStringLiteral("pi"), QProcessEnvironment(),
            QStringLiteral("https://example.invalid/"), QStringLiteral("/home/pi"));

        const QString dirs = dataDirsIn(args);
        INFO("XDG_DATA_DIRS=" << dirs.toStdString());
        REQUIRE_FALSE(dirs.isEmpty());

        // The specification's own default has to survive the rebuild, or a
        // distribution-packaged browser stops being found in the course of
        // making a Snap one findable.
        CHECK(dirs.split(QLatin1Char(':')).contains(QStringLiteral("/usr/share")));
    }

    SECTION("only directories that are really there are named")
    {
        // A machine with no Flatpak and no Snap should not be handed paths
        // that do not exist; xdg-open would stat each one for nothing, and a
        // wrong value is harder to debug than a short one.
        const QStringList args = buildOpenUrlAsUserArgs(
            QStringLiteral("pi"), QProcessEnvironment(),
            QStringLiteral("https://example.invalid/"), QStringLiteral("/home/pi"));

        const QStringList dirs = dataDirsIn(args).split(QLatin1Char(':'),
                                                        Qt::SkipEmptyParts);
        REQUIRE_FALSE(dirs.isEmpty());
        for (const QString& dir : dirs) {
            INFO(dir.toStdString());
            CHECK(QFileInfo(dir).isDir());
        }
    }

    SECTION("a value that did survive is used as it stands")
    {
        // Running under sudo rather than pkexec, or a desktop that exported
        // it some other way. The session's own answer beats anything guessed
        // here.
        QProcessEnvironment env = elevatedRootEnvironment();
        env.insert(QStringLiteral("XDG_DATA_DIRS"),
                   QStringLiteral("/opt/thing/share:/usr/share"));

        const QStringList args = buildOpenUrlAsUserArgs(
            QStringLiteral("pi"), env, QStringLiteral("https://example.invalid/"),
            QStringLiteral("/home/pi"));

        CHECK(dataDirsIn(args) == QStringLiteral("/opt/thing/share:/usr/share"));
    }

    SECTION("the desktop's own opener is named when it is known")
    {
        // XDG_CURRENT_DESKTOP picks xdg-open's backend -- gio, kde-open,
        // exo-open. Without it xdg-open takes its generic path and ignores
        // the desktop's own handler.
        QProcessEnvironment env = elevatedRootEnvironment();
        env.insert(QStringLiteral("XDG_CURRENT_DESKTOP"), QStringLiteral("KDE"));

        const QStringList args = buildOpenUrlAsUserArgs(
            QStringLiteral("pi"), env, QStringLiteral("https://example.invalid/"),
            QStringLiteral("/home/pi"));

        CHECK(args.contains(QStringLiteral("XDG_CURRENT_DESKTOP=KDE")));
    }

    SECTION("and not invented when it is not")
    {
        const QStringList args = buildOpenUrlAsUserArgs(
            QStringLiteral("pi"), elevatedRootEnvironment(),
            QStringLiteral("https://example.invalid/"), QStringLiteral("/home/pi"));

        for (const QString& arg : args)
            CHECK_FALSE(arg.startsWith(QStringLiteral("XDG_CURRENT_DESKTOP=")));
    }
}

TEST_CASE("The URL stays a single argument", "[platformquirks][openurl]")
{
    using PlatformQuirks::TestAPI::buildOpenUrlAsUserArgs;

    // There is no shell in this chain -- runuser, env and xdg-open are each
    // handed a list -- so a URL with a space, an ampersand or a semicolon in
    // it arrives as one argument and is not interpreted by anything.
    const QString awkward =
        QStringLiteral("https://example.invalid/a b?x=1&y=2;rm -rf /");
    const QStringList args = buildOpenUrlAsUserArgs(
        QStringLiteral("pi"), elevatedRootEnvironment(), awkward,
        QStringLiteral("/home/pi"));

    CHECK(args.last() == awkward);
    CHECK(args.count(awkward) == 1);
}

#endif // Q_OS_LINUX
