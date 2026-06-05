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

TEST_CASE("Multiple start/stop cycles work correctly", "[platformquirks][network]") {
    for (int i = 0; i < 3; i++) {
        PlatformQuirks::startNetworkMonitoring([](bool) {});
        PlatformQuirks::stopNetworkMonitoring();
    }
}
