/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Unit tests for FastbootFlashThread's destructive paths.
 *
 * The erase path is the most dangerous code in the fastboot flow: it
 * bare-wipes a whole block device and rewrites its partition table. Two
 * properties matter to a user and are pinned here:
 *
 *   1. An erase happens ONLY for the exact "internal://format" sentinel.
 *      Anything else -- a real image URL, a near-miss, a URL that merely
 *      contains the sentinel -- must take the normal flash path.
 *
 *   2. Once an erase starts, a cancel between any two steps stops it and
 *      never reports success. Each fastboot command is separately
 *      destructive, so "cancelled" must mean the *next* one does not run.
 *
 * Everything is driven through MockUsbTransport, so no device is touched.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_session.hpp>

#include "fastbootflashthread.h"
#include "fastboot/fastboot_protocol.h"
#include "rpiboot/test/mock_usb_transport.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

using namespace rpiboot::testing;
using Catch::Matchers::ContainsSubstring;

namespace {

// ── Test seams ────────────────────────────────────────────────

// Exposes the protected decision points. Nothing is overridden: these are
// the shipping implementations, just reachable without a USB bus.
class TestableFlashThread : public FastbootFlashThread
{
public:
    explicit TestableFlashThread(const QUrl& imageUrl,
                                 const QString& blockDevice = QStringLiteral("mmcblk0"))
        : FastbootFlashThread(QStringLiteral("0001-fastboot"), blockDevice,
                              imageUrl, 0, 0, QByteArray())
    {
    }

    using FastbootFlashThread::isEraseOperation;
    using FastbootFlashThread::performErase;
    using FastbootFlashThread::applyCustomisation;
};

// A transport that can run a callback after each command reaches the "wire",
// which is how a cancel is injected precisely between two destructive steps.
class HookedTransport : public MockUsbTransport
{
public:
    std::function<void(int)> afterWrite;   // receives the 1-based write count

    int bulkWrite(uint8_t endpoint, std::span<const uint8_t> data, int timeoutMs) override
    {
        const int rc = MockUsbTransport::bulkWrite(endpoint, data, timeoutMs);
        ++_writeCount;
        if (afterWrite)
            afterWrite(_writeCount);
        return rc;
    }

private:
    int _writeCount = 0;
};

// Collects the signals performErase() reports its outcome through.
struct SignalLog
{
    bool success = false;
    bool finalizing = false;
    QStringList errors;
    QStringList status;

    void attach(FastbootFlashThread* t)
    {
        QObject::connect(t, &FastbootFlashThread::success, [this] { success = true; });
        QObject::connect(t, &FastbootFlashThread::finalizing, [this] { finalizing = true; });
        QObject::connect(t, &FastbootFlashThread::error,
                         [this](QString m) { errors << m; });
        QObject::connect(t, &FastbootFlashThread::preparationStatusUpdate,
                         [this](QString m) { status << m; });
    }
};

std::vector<uint8_t> okay(const std::string& payload = "")
{
    const std::string r = "OKAY" + payload;
    return {r.begin(), r.end()};
}

std::vector<uint8_t> fail(const std::string& reason)
{
    const std::string r = "FAIL" + reason;
    return {r.begin(), r.end()};
}

// The fastboot commands that actually reached the transport, in order.
QStringList commandsSent(const MockUsbTransport& mock)
{
    QStringList out;
    for (const auto& w : mock.capturedBulkWrites())
        out << QString::fromUtf8(reinterpret_cast<const char*>(w.data()),
                                 static_cast<int>(w.size()));
    return out;
}

// Queue N successful command responses.
void queueOkays(MockUsbTransport& mock, int n)
{
    for (int i = 0; i < n; ++i)
        mock.queueBulkReadResponse(okay());
}

constexpr const char* kEraseSentinel = "internal://format";

} // namespace

// ══════════════════════════════════════════════════════════════
// 1. The erase trigger
//
// isEraseOperation() is the single gate between "flash an image" and
// "destroy everything on the device", so it is tested against near-misses
// rather than just the happy case.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Erase triggers on the exact sentinel URL", "[fastboot][erase]")
{
    TestableFlashThread t{QUrl(QString::fromLatin1(kEraseSentinel))};
    CHECK(t.isEraseOperation());
}

TEST_CASE("Erase does not trigger for a real image URL", "[fastboot][erase]")
{
    TestableFlashThread t{QUrl(QStringLiteral("https://downloads.raspberrypi.org/os.img.xz"))};
    CHECK_FALSE(t.isEraseOperation());
}

TEST_CASE("Erase does not trigger for near-miss URLs", "[fastboot][erase]")
{
    // Each of these has been mistaken for the sentinel by a substring or
    // case-insensitive comparison at some point in similar code. A false
    // positive here wipes a user's device instead of flashing it.
    const QStringList nearMisses = {
        QStringLiteral(""),                                  // no image selected
        QStringLiteral("internal://format/"),                // trailing slash
        QStringLiteral("internal://formatted"),              // longer word
        QStringLiteral("internal://format?confirm=1"),       // query string
        QStringLiteral("internal://reboot"),                 // different sentinel
        QStringLiteral("https://example.com/internal://format"), // sentinel as substring
        QStringLiteral("file:///tmp/internal://format"),     // ditto, local file
        QStringLiteral(" internal://format"),                // leading space
    };

    for (const QString& url : nearMisses) {
        TestableFlashThread t{QUrl(url)};
        INFO("URL: " << url.toStdString());
        CHECK_FALSE(t.isEraseOperation());
    }
}

TEST_CASE("Erase trigger is case-insensitive in scheme and host", "[fastboot][erase]")
{
    // Not a laxness in the comparison: QUrl normalises the scheme and host
    // to lower case because RFC 3986 defines both as case-insensitive, so
    // these are literally the same URL as the sentinel. Pinned so that a
    // future "tighten this up" into a case-sensitive raw string compare --
    // which would silently stop recognising the erase entry -- shows up here.
    for (const QString& url : {QStringLiteral("INTERNAL://FORMAT"),
                               QStringLiteral("Internal://Format"),
                               QStringLiteral("internal://FORMAT")}) {
        TestableFlashThread t{QUrl(url)};
        INFO("URL: " << url.toStdString());
        CHECK(t.isEraseOperation());
    }
}

// ══════════════════════════════════════════════════════════════
// 2. The erase command sequence
// ══════════════════════════════════════════════════════════════

TEST_CASE("Erase issues wipe, partition table, partition, reboot in order",
          "[fastboot][erase]")
{
    TestableFlashThread t{QUrl(QString::fromLatin1(kEraseSentinel)), QStringLiteral("mmcblk0")};
    SignalLog log;
    log.attach(&t);

    MockUsbTransport mock;
    queueOkays(mock, 4);

    fastboot::FastbootProtocol fb;
    CHECK(t.performErase(fb, mock));

    const QStringList cmds = commandsSent(mock);
    REQUIRE(cmds.size() == 4);
    CHECK(cmds[0] == QStringLiteral("erase:mmcblk0"));
    CHECK(cmds[1] == QStringLiteral("oem partinit mmcblk0 dos"));
    CHECK(cmds[2] == QStringLiteral("oem partapp mmcblk0 c"));
    CHECK(cmds[3] == QStringLiteral("reboot"));

    CHECK(log.success);
    CHECK(log.finalizing);
    CHECK(log.errors.isEmpty());
    // The user is told what is happening before each destructive step.
    CHECK(log.status.size() == 3);
}

TEST_CASE("Erase targets the whole disk, not a partition", "[fastboot][erase]")
{
    // The flash path mounts "<dev>p1", so a stray partition suffix here
    // would wipe the wrong thing.
    TestableFlashThread t{QUrl(QString::fromLatin1(kEraseSentinel)), QStringLiteral("nvme0n1")};
    MockUsbTransport mock;
    queueOkays(mock, 4);

    fastboot::FastbootProtocol fb;
    CHECK(t.performErase(fb, mock));

    const QStringList cmds = commandsSent(mock);
    REQUIRE(cmds.size() == 4);
    CHECK(cmds[0] == QStringLiteral("erase:nvme0n1"));
    CHECK(cmds[1] == QStringLiteral("oem partinit nvme0n1 dos"));
    CHECK(cmds[2] == QStringLiteral("oem partapp nvme0n1 c"));
}

TEST_CASE("Erase stops at the failing step and reports it", "[fastboot][erase]")
{
    struct Case {
        const char* name;
        int okaysBefore;        // successful commands before the failure
        const char* deviceMsg;  // what the device says
        const char* userMsg;    // what the user should be told
    };

    auto c = GENERATE(
        Case{"wipe",            0, "device busy",   "erase"},
        Case{"partition table", 1, "write protect", "partition table"},
        Case{"partition",       2, "no space",      "partition"});

    INFO("failing step: " << c.name);

    TestableFlashThread t{QUrl(QString::fromLatin1(kEraseSentinel))};
    SignalLog log;
    log.attach(&t);

    MockUsbTransport mock;
    queueOkays(mock, c.okaysBefore);
    mock.queueBulkReadResponse(fail(c.deviceMsg));

    fastboot::FastbootProtocol fb;
    CHECK_FALSE(t.performErase(fb, mock));

    // No command after the failing one is attempted.
    CHECK(commandsSent(mock).size() == c.okaysBefore + 1);
    CHECK_FALSE(log.success);
    REQUIRE(log.errors.size() == 1);
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring(c.userMsg));
    // The device's own reason is surfaced, not swallowed.
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring(c.deviceMsg));
}

TEST_CASE("Erase succeeds even if the final reboot fails", "[fastboot][erase]")
{
    // The wipe and repartition have already landed by this point; a device
    // that does not ack the reboot has still been erased, so failing here
    // would tell the user their erase failed when it did not.
    TestableFlashThread t{QUrl(QString::fromLatin1(kEraseSentinel))};
    SignalLog log;
    log.attach(&t);

    MockUsbTransport mock;
    queueOkays(mock, 3);
    mock.queueBulkReadResponse(fail("rebooting"));

    fastboot::FastbootProtocol fb;
    CHECK(t.performErase(fb, mock));
    CHECK(log.success);
    CHECK(log.errors.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// 3. Cancellation between destructive steps
//
// Every step is irreversible, so a cancel arriving mid-sequence must stop
// the next one from running and must never report success.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Cancel before erase starts issues no commands", "[fastboot][erase][cancel]")
{
    TestableFlashThread t{QUrl(QString::fromLatin1(kEraseSentinel))};
    SignalLog log;
    log.attach(&t);
    t.cancel();

    MockUsbTransport mock;
    queueOkays(mock, 4);

    fastboot::FastbootProtocol fb;
    CHECK_FALSE(t.performErase(fb, mock));

    CHECK(commandsSent(mock).isEmpty());
    CHECK_FALSE(log.success);
    REQUIRE(log.errors.size() == 1);
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("Cancel"));
}

TEST_CASE("Cancel after a step stops the sequence there",
          "[fastboot][erase][cancel]")
{
    // Cancel lands after the Nth command completes; command N+1 must not run.
    const int cancelAfter = GENERATE(1, 2, 3);
    INFO("cancelling after command " << cancelAfter);

    TestableFlashThread t{QUrl(QString::fromLatin1(kEraseSentinel))};
    SignalLog log;
    log.attach(&t);

    HookedTransport mock;
    queueOkays(mock, 4);
    mock.afterWrite = [&t, cancelAfter](int n) {
        if (n == cancelAfter)
            t.cancel();
    };

    fastboot::FastbootProtocol fb;
    CHECK_FALSE(t.performErase(fb, mock));

    CHECK(commandsSent(mock).size() == cancelAfter);
    CHECK_FALSE(log.success);
    REQUIRE(log.errors.size() == 1);
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("Cancel"));
}

TEST_CASE("A cancelled erase reports cancellation, not a device failure",
          "[fastboot][erase][cancel]")
{
    // When a cancel and a device error coincide, the user should be told
    // they cancelled -- not handed a scary "failed to erase" message.
    TestableFlashThread t{QUrl(QString::fromLatin1(kEraseSentinel))};
    SignalLog log;
    log.attach(&t);

    HookedTransport mock;
    mock.queueBulkReadResponse(fail("aborted"));
    mock.afterWrite = [&t](int) { t.cancel(); };

    fastboot::FastbootProtocol fb;
    CHECK_FALSE(t.performErase(fb, mock));

    CHECK_FALSE(log.success);
    REQUIRE(log.errors.size() == 1);
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("Cancel"));
}

// ══════════════════════════════════════════════════════════════
// 4. OS customisation
//
// Customisation is the wifi password, SSH key and hostname the user typed
// in before flashing. Losing it quietly is the bad outcome: the board comes
// up with no network and no way in, and the flash reported success.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Customisation is skipped when there is nothing to apply",
          "[fastboot][customise]")
{
    TestableFlashThread t{QUrl(QStringLiteral("https://example.com/os.img.xz"))};
    SignalLog log;
    log.attach(&t);

    MockUsbTransport mock;
    fastboot::FastbootProtocol fb;

    // No setImageCustomisation() call: nothing to write, so the boot
    // partition is not even mounted.
    CHECK(t.applyCustomisation(fb, mock));
    CHECK(commandsSent(mock).isEmpty());
    CHECK(log.errors.isEmpty());
}

TEST_CASE("Customisation without an init format is skipped, not written",
          "[fastboot][customise]")
{
    // initFormat is what says *how* to express the settings on this OS.
    // Without it there is nothing meaningful to write, so the flash goes
    // ahead unmodified rather than writing something the OS ignores.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.com/os.img.xz"))};
    SignalLog log;
    log.attach(&t);
    t.setImageCustomisation("arm_64bit=1", QByteArray(), QByteArray(),
                            QByteArray(), QByteArray(), QByteArray());

    MockUsbTransport mock;
    fastboot::FastbootProtocol fb;

    CHECK(t.applyCustomisation(fb, mock));
    CHECK(commandsSent(mock).isEmpty());
}

TEST_CASE("Customisation reports a boot partition it cannot mount",
          "[fastboot][customise]")
{
    // The user asked for settings and they cannot be applied. Returning
    // success here would flash the board and drop the configuration on the
    // floor without telling anyone.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.com/os.img.xz")),
                          QStringLiteral("mmcblk0")};
    SignalLog log;
    log.attach(&t);
    t.setImageCustomisation("arm_64bit=1", QByteArray(), QByteArray(),
                            QByteArray(), QByteArray(), "systemd");

    MockUsbTransport mock;
    mock.queueBulkReadResponse(fail("no such partition"));

    fastboot::FastbootProtocol fb;
    CHECK_FALSE(t.applyCustomisation(fb, mock));

    REQUIRE(log.errors.size() == 1);
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("mount"));
    // The device's reason reaches the user rather than a generic failure.
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("no such partition"));

    // It mounts the boot partition of the target, not the whole device.
    const QStringList cmds = commandsSent(mock);
    REQUIRE(cmds.size() == 1);
    CHECK_THAT(cmds[0].toStdString(), ContainsSubstring("oem mount"));
    CHECK_THAT(cmds[0].toStdString(), ContainsSubstring("mmcblk0p1"));
}

// ── main(): a QCoreApplication must exist before any QObject-based case ──

int main(int argc, char* argv[])
{
    int argcCopy = argc;
    QCoreApplication app(argcCopy, argv);
    return Catch::Session().run(argc, argv);
}
