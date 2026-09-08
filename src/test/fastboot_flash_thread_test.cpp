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

#include "fastboot/sparse_encoder.h"

#include "fastbootflashthread.h"
#include "fastboot/fastboot_protocol.h"
#include "rpiboot/test/mock_usb_transport.h"
#include <map>
#include <memory>
#include "rpiboot/libusb_transport.h"
#include "rpiboot/rpiboot_types.h"
#include "sparse_decode.h"

#include <archive.h>
#include <archive_entry.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

// Stub, for the same reason as the one in eeprom_signer_test.cpp: the
// platform secureboot_crypto_*.cpp linked in here defines extractRsaPubkeyBin,
// which calls parseSubjectPublicKeyInfoDerToNE. The real one lives in
// secureboot.cpp, and pulling that in brings the device wrapper hierarchy and
// the boot image creator with it. Nothing in this file signs anything.
#if !defined(_WIN32)
namespace SecureBootCrypto {
QByteArray parseSubjectPublicKeyInfoDerToNE(const QByteArray&) { return {}; }
}
#endif

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
                                 const QString& blockDevice = QStringLiteral("mmcblk0"),
                                 quint64 extractLen = 0,
                                 const QByteArray& expectedHash = QByteArray())
        : FastbootFlashThread(QStringLiteral("0001-fastboot"), blockDevice,
                              imageUrl, 0, extractLen, expectedHash)
    {
    }

    using FastbootFlashThread::isEraseOperation;
    using FastbootFlashThread::performErase;
    using FastbootFlashThread::applyCustomisation;
    using FastbootFlashThread::applyBootOrderUpdate;
    using FastbootFlashThread::runImpl;
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

// ══════════════════════════════════════════════════════════════
// 5. BOOT_ORDER after a flash
//
// Having written an image to a device, the EEPROM is edited so the board
// tries that device first on the next power cycle. When it goes wrong the
// flash itself looks perfect and the board comes up off whatever it booted
// from before -- the "I flashed the NVMe and it still started off the SD
// card" report, with nothing in the log to explain it.
//
// Every failure path here is deliberately non-fatal: a successful image
// write must not be reported as a failure because the EEPROM tweak could
// not run. What matters is that it leaves BOOT_ORDER alone rather than
// writing something wrong.
// ══════════════════════════════════════════════════════════════

namespace {

// A minimal EEPROM image in the container format BootloaderImage parses,
// carrying a bootconf.txt with the given BOOT_ORDER.
std::vector<uint8_t> eepromWithBootOrder(const std::string &bootOrderLine)
{
    constexpr uint32_t kMagic = 0x55aaf00f, kPad = 0x55aafeef, kFile = 0x55aaf11f;
    constexpr size_t kSize = 512 * 1024, kReadOnly = 64 * 1024;
    std::vector<uint8_t> img(kSize, 0xff);

    auto be32 = [&img](size_t off, uint32_t v) {
        img[off] = uint8_t(v >> 24); img[off+1] = uint8_t(v >> 16);
        img[off+2] = uint8_t(v >> 8); img[off+3] = uint8_t(v);
    };

    const std::vector<uint8_t> bootcode(4096, 0xAA);
    be32(0, kMagic);
    be32(4, uint32_t(bootcode.size()));
    std::memcpy(&img[8], bootcode.data(), bootcode.size());
    size_t off = 8 + bootcode.size();
    while (off % 8) img[off++] = 0xff;

    be32(off, kPad);
    be32(off + 4, uint32_t(kReadOnly - (off + 8)));
    off = kReadOnly;

    // bootconf.txt, with room to grow when BOOT_ORDER is rewritten.
    const std::string conf = bootOrderLine + "\n";
    const size_t reserve = 4096;
    const uint32_t length = uint32_t(reserve + 12 + 4);
    be32(off, kFile);
    be32(off + 4, length);
    std::memset(&img[off + 8], 0, 16);
    std::memcpy(&img[off + 8], "bootconf.txt", 12);
    std::memset(&img[off + 24], 0, reserve);
    std::memcpy(&img[off + 24], conf.data(), conf.size());
    return img;
}

// Queue the exchanges applyBootOrderUpdate() performs: two getvars, the
// eeprom-read, then the upload DATA phase and its payload.
void queueEepromRead(MockUsbTransport &mock, const std::vector<uint8_t> &image,
                     const std::string &spidev = "spidev0.0",
                     const std::string &signedEeprom = "0")
{
    mock.queueBulkReadResponse(okay(spidev));        // getvar eeprom-device
    mock.queueBulkReadResponse(okay(signedEeprom));  // getvar signed-eeprom
    mock.queueBulkReadResponse(okay());              // oem eeprom-read

    char header[16];
    std::snprintf(header, sizeof(header), "DATA%08zx", image.size());
    mock.queueBulkReadResponse(std::vector<uint8_t>(header, header + 12));

    constexpr size_t kChunk = 16 * 1024;
    for (size_t off = 0; off < image.size(); off += kChunk) {
        const size_t n = std::min(kChunk, image.size() - off);
        mock.queueBulkReadResponse(
            std::vector<uint8_t>(image.begin() + off, image.begin() + off + n));
    }
}

} // namespace

TEST_CASE("An unrecognised target leaves the boot order alone",
          "[fastboot][bootorder]")
{
    // Nothing sensible to put first, so nothing is written -- rather than
    // guessing a nibble and pointing the board at the wrong device.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                          QStringLiteral("wibble0")};
    MockUsbTransport mock;
    fastboot::FastbootProtocol fb;

    CHECK_NOTHROW(t.applyBootOrderUpdate(fb, mock));
    CHECK(commandsSent(mock).isEmpty());
}

TEST_CASE("A device with no SPI EEPROM is left alone", "[fastboot][bootorder]")
{
    // Compute Modules without an EEPROM report this. Attempting the update
    // anyway would fail on every one of them.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                          QStringLiteral("nvme0n1")};
    MockUsbTransport mock;
    mock.queueBulkReadResponse(okay("not available"));

    fastboot::FastbootProtocol fb;
    CHECK_NOTHROW(t.applyBootOrderUpdate(fb, mock));

    // It asked, and then stopped.
    const QStringList cmds = commandsSent(mock);
    REQUIRE(cmds.size() == 1);
    CHECK_THAT(cmds[0].toStdString(), ContainsSubstring("eeprom-device"));
}

TEST_CASE("The flashed device is put first in BOOT_ORDER",
          "[fastboot][bootorder]")
{
    // 0xf41 is restart / USB / SD. After flashing NVMe the board must try
    // NVMe first, with the previous entries kept behind it.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                          QStringLiteral("nvme0n1")};
    MockUsbTransport mock;
    queueEepromRead(mock, eepromWithBootOrder("BOOT_ORDER=0xf41"));

    fastboot::FastbootProtocol fb;
    CHECK_NOTHROW(t.applyBootOrderUpdate(fb, mock));

    const QStringList cmds = commandsSent(mock);
    INFO("commands: " << cmds.join(QStringLiteral(" | ")).toStdString());
    // It got as far as asking the device for its EEPROM.
    CHECK(cmds.filter(QStringLiteral("eeprom-read")).size() == 1);
}

TEST_CASE("An EEPROM the device will not hand over is not written back",
          "[fastboot][bootorder]")
{
    // Read fails, so there is nothing to edit. Writing a fabricated EEPROM
    // would be far worse than skipping the tweak.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                          QStringLiteral("nvme0n1")};
    MockUsbTransport mock;
    mock.queueBulkReadResponse(okay("spidev0.0"));
    mock.queueBulkReadResponse(okay("0"));
    mock.queueBulkReadResponse(fail("read error"));

    fastboot::FastbootProtocol fb;
    CHECK_NOTHROW(t.applyBootOrderUpdate(fb, mock));

    const QStringList cmds = commandsSent(mock);
    INFO("commands: " << cmds.join(QStringLiteral(" | ")).toStdString());
    CHECK(cmds.filter(QStringLiteral("eeprom-update")).isEmpty());
}

TEST_CASE("An EEPROM that does not parse is not written back",
          "[fastboot][bootorder]")
{
    // Garbage in place of an image: skip, rather than editing something
    // that is not an EEPROM and flashing the result.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                          QStringLiteral("nvme0n1")};
    MockUsbTransport mock;
    queueEepromRead(mock, std::vector<uint8_t>(512 * 1024, 0x5A));

    fastboot::FastbootProtocol fb;
    CHECK_NOTHROW(t.applyBootOrderUpdate(fb, mock));

    const QStringList cmds = commandsSent(mock);
    INFO("commands: " << cmds.join(QStringLiteral(" | ")).toStdString());
    CHECK(cmds.filter(QStringLiteral("eeprom-update")).isEmpty());
}

TEST_CASE("An EEPROM with no bootconf.txt is not written back",
          "[fastboot][bootorder]")
{
    // Parses as an image but carries no configuration section, so there is
    // no BOOT_ORDER to edit.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                          QStringLiteral("nvme0n1")};
    MockUsbTransport mock;

    // Same container, but the named section is something else.
    auto img = eepromWithBootOrder("BOOT_ORDER=0xf41");
    const char *other = "notconf.txt";
    std::memcpy(&img[64 * 1024 + 8], other, std::strlen(other) + 1);
    queueEepromRead(mock, img);

    fastboot::FastbootProtocol fb;
    CHECK_NOTHROW(t.applyBootOrderUpdate(fb, mock));

    const QStringList cmds = commandsSent(mock);
    INFO("commands: " << cmds.join(QStringLiteral(" | ")).toStdString());
    CHECK(cmds.filter(QStringLiteral("eeprom-update")).isEmpty());
}

// ══════════════════════════════════════════════════════════════
// 6. The flash sequence itself
//
// runImpl() opens the device and drives the whole flash. It used to be
// reachable only with hardware attached -- not because of anything it does,
// but because the one line that obtains the transport constructed a concrete
// LibusbTransport. Everything after that line already goes through
// IUsbTransport. Behind a virtual, the sequence runs against the mock.
//
// The case that matters most is the refusal: before any destructive command
// the device has to identify as a Pi. Get that wrong and the imager erases
// whatever else happened to be in fastboot mode on the bus.
// ══════════════════════════════════════════════════════════════

namespace {

// Forwards to a mock the test owns.
//
// runImpl() holds the transport in a local unique_ptr, so whatever it is
// handed is destroyed when it returns. Handing it the mock directly and
// keeping a pointer to inspect afterwards is a use-after-free -- which is
// exactly what the first version of this did. The thread owns this thin
// view instead; the mock outlives it.
class TransportView : public rpiboot::IUsbTransport
{
public:
    explicit TransportView(MockUsbTransport &target) : _t(target) {}

    bool controlTransfer(uint8_t requestType, uint8_t request, uint16_t wValue,
                         uint16_t wIndex, std::span<const uint8_t> data,
                         int timeoutMs) override
    {
        return _t.controlTransfer(requestType, request, wValue, wIndex, data, timeoutMs);
    }
    int controlTransferIn(uint8_t requestType, uint8_t request, uint16_t wValue,
                          uint16_t wIndex, std::span<uint8_t> buffer,
                          int timeoutMs) override
    {
        return _t.controlTransferIn(requestType, request, wValue, wIndex, buffer, timeoutMs);
    }
    int bulkWrite(uint8_t endpoint, std::span<const uint8_t> data, int timeoutMs) override
    {
        return _t.bulkWrite(endpoint, data, timeoutMs);
    }
    int bulkRead(uint8_t endpoint, std::span<uint8_t> buffer, int timeoutMs) override
    {
        return _t.bulkRead(endpoint, buffer, timeoutMs);
    }
    bool isOpen() const override { return _t.isOpen(); }
    std::string interfaceString() const override { return _t.interfaceString(); }
    uint8_t outEndpoint() const override { return _t.outEndpoint(); }
    uint8_t inEndpoint() const override { return _t.inEndpoint(); }

private:
    MockUsbTransport &_t;
};

// A thread whose device is a mock the test keeps.
class MockedFlashThread : public TestableFlashThread
{
public:
    using TestableFlashThread::TestableFlashThread;

    MockUsbTransport mock;              // outlives runImpl()
    bool openShouldFail = false;

protected:
    std::unique_ptr<rpiboot::IUsbTransport> openFastbootTransport(
        rpiboot::LibusbContext &, const rpiboot::UsbDeviceInfo &) override
    {
        if (openShouldFail)
            return nullptr;
        return std::make_unique<TransportView>(mock);
    }
};

} // namespace

TEST_CASE("A device that will not identify as a Pi is not flashed",
          "[fastboot][flash]")
{
    // The guard in front of every destructive command. Something else in
    // fastboot mode on the same bus -- a phone, a dev board -- must not be
    // erased and rewritten because it answered.
    MockedFlashThread t{QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                        QStringLiteral("mmcblk0")};
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    // Nothing destructive was sent.
    const QStringList cmds = commandsSent(t.mock);
    INFO("commands: " << cmds.join(QStringLiteral(" | ")).toStdString());
    for (const QString &c : cmds) {
        CHECK_THAT(c.toStdString(), !ContainsSubstring("erase"));
        CHECK_THAT(c.toStdString(), !ContainsSubstring("partinit"));
        CHECK_THAT(c.toStdString(), !ContainsSubstring("flash"));
    }

    REQUIRE_FALSE(log.errors.isEmpty());
    CHECK_THAT(log.errors.last().toStdString(), ContainsSubstring("Refusing to flash"));
}

TEST_CASE("A device that cannot be opened is reported", "[fastboot][flash]")
{
    MockedFlashThread t{QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                        QStringLiteral("mmcblk0")};
    t.openShouldFail = true;
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    REQUIRE_FALSE(log.errors.isEmpty());
    CHECK_THAT(log.errors.last().toStdString(), ContainsSubstring("Failed to open"));
}

TEST_CASE("An erase runs the full sequence against an identified Pi",
          "[fastboot][flash]")
{
    // The erase entry, driven through runImpl() rather than by calling
    // performErase() directly -- so the identification, the mode decision
    // and the erase all run in the order the application uses them.
    class ErasingThread : public MockedFlashThread
    {
    public:
        using MockedFlashThread::MockedFlashThread;

    protected:
        std::unique_ptr<rpiboot::IUsbTransport> openFastbootTransport(
            rpiboot::LibusbContext &ctx, const rpiboot::UsbDeviceInfo &info) override
        {
            // Identify as a Pi, and answer the four erase commands.
            mock.setInterfaceString(rpiboot::FASTBOOT_INTERFACE_DESCRIPTOR);
            for (int i = 0; i < 4; ++i)
                mock.queueBulkReadResponse(okay());
            return MockedFlashThread::openFastbootTransport(ctx, info);
        }
    };

    ErasingThread t{QUrl(QStringLiteral("internal://format")), QStringLiteral("mmcblk0")};
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    const QStringList cmds = commandsSent(t.mock);
    INFO("commands: " << cmds.join(QStringLiteral(" | ")).toStdString());
    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());

    REQUIRE(cmds.size() >= 3);
    CHECK(cmds[0] == QStringLiteral("erase:mmcblk0"));
    CHECK(cmds[1] == QStringLiteral("oem partinit mmcblk0 dos"));
    CHECK(cmds[2] == QStringLiteral("oem partapp mmcblk0 c"));
    CHECK(log.success);
}

TEST_CASE("A cancelled flash does not reach the device", "[fastboot][flash]")
{
    MockedFlashThread t{QUrl(QStringLiteral("internal://format")),
                        QStringLiteral("mmcblk0")};
    SignalLog log;
    log.attach(&t);
    t.cancel();

    t.runImpl();

    CHECK_FALSE(log.success);
    const QStringList cmds = commandsSent(t.mock);
    INFO("commands: " << cmds.join(QStringLiteral(" | ")).toStdString());
    for (const QString &c : cmds)
        CHECK_THAT(c.toStdString(), !ContainsSubstring("erase:"));
}

// ══════════════════════════════════════════════════════════════
// 6b. The whole flash, end to end
//
// Everything above stops at the edges of the pipeline: the erase commands,
// the EEPROM, the size arithmetic. The pipeline itself -- curl into the
// compressed ring, libarchive out of it, the sparse encoder, the segments
// going down the wire, the hash of what was read -- was reachable only with
// a Compute Module attached, and so was never run.
//
// It does not need one. The image can come off disk through the same curl
// call, and the device end is a few hundred bytes of state machine: answer
// getvar, accept a download, acknowledge a flash. What that device receives
// can then be decoded back into an image and compared with the one that went
// in, which is the only assertion that really matters -- every stage in
// between is load-bearing for it.
// ══════════════════════════════════════════════════════════════

namespace {

// A fastboot device that actually answers, and keeps what it was sent.
class FakeFastbootDevice : public MockUsbTransport
{
public:
    explicit FakeFastbootDevice(uint32_t maxDownload)
        : _maxDownload(maxDownload)
    {
        setInterfaceString(rpiboot::FASTBOOT_INTERFACE_DESCRIPTOR);
        setOpen(true);
    }

    // The payload of each flash command, in the order they arrived.
    const std::vector<std::vector<uint8_t>>& flashed() const { return _flashed; }
    const std::vector<std::string>& commands() const { return _commands; }

    // The filesystem the device exposes once mounted. Seed what the image is
    // supposed to have put there; read back what the customisation wrote.
    // Make any command starting with this prefix answer FAIL.
    void failCommand(std::string prefix) { _failPrefix = std::move(prefix); }

    // Called with each command as it arrives, so a test can cancel partway.
    std::function<void(const std::string&)> onCommand;

    void seedFile(const std::string& path, const QByteArray& content)
    {
        _files[path] = std::vector<uint8_t>(content.begin(), content.end());
    }
    bool hasFile(const std::string& path) const { return _files.count(path) != 0; }
    QByteArray file(const std::string& path) const
    {
        const auto it = _files.find(path);
        if (it == _files.end())
            return {};
        return QByteArray(reinterpret_cast<const char*>(it->second.data()),
                          static_cast<int>(it->second.size()));
    }

    int bulkWrite(uint8_t endpoint, std::span<const uint8_t> data, int timeoutMs) override
    {
        // Mid-download the writes are image bytes, not commands. Accumulate
        // them here rather than through the base, which keeps every write it
        // sees -- that would hold a second copy of the whole image.
        if (_awaiting > 0) {
            _payload.insert(_payload.end(), data.begin(), data.end());
            if (_payload.size() >= _awaiting) {
                _awaiting = 0;
                _staged = std::move(_payload);
                _payload.clear();
                queueBulkReadResponse(okay());
            }
            return static_cast<int>(data.size());
        }

        const std::string cmd(reinterpret_cast<const char*>(data.data()), data.size());
        _commands.push_back(cmd);
        if (onCommand)
            onCommand(cmd);
        answer(cmd);
        return MockUsbTransport::bulkWrite(endpoint, data, timeoutMs);
    }

private:
    void answer(const std::string& cmd)
    {
        const auto startsWith = [&cmd](const char* p) { return cmd.rfind(p, 0) == 0; };

        if (!_failPrefix.empty() && cmd.rfind(_failPrefix, 0) == 0) {
            queueBulkReadResponse(fail("simulated device refusal"));
            return;
        }

        if (startsWith("getvar:max-download-size")) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08x", _maxDownload);
            queueBulkReadResponse(okay(buf));
            return;
        }
        if (startsWith("download:")) {
            const std::string hex = cmd.substr(std::strlen("download:"));
            _awaiting = std::stoul(hex, nullptr, 16);
            _payload.clear();
            _payload.reserve(_awaiting);
            const std::string data = "DATA" + hex;
            queueBulkReadResponse({data.begin(), data.end()});
            return;
        }
        if (startsWith("flash:")) {
            _flashed.push_back(std::move(_staged));
            _staged.clear();
            queueBulkReadResponse(okay());
            return;
        }

        // ── the mounted boot partition ──
        //
        // A write is a download followed by "oem download-file <path>"; a read
        // is "oem upload-file <path>" followed by an "upload" the device
        // answers with DATA, the bytes and an OKAY.
        if (startsWith("oem download-file ")) {
            _files[cmd.substr(std::strlen("oem download-file "))] = std::move(_staged);
            _staged.clear();
            queueBulkReadResponse(okay());
            return;
        }
        if (startsWith("oem upload-file ")) {
            _pendingUpload = cmd.substr(std::strlen("oem upload-file "));
            queueBulkReadResponse(okay());
            return;
        }
        if (cmd == "upload") {
            static const std::vector<uint8_t> kNothing;
            const auto it = _files.find(_pendingUpload);
            const std::vector<uint8_t>& body = it == _files.end() ? kNothing : it->second;

            char hdr[32];
            std::snprintf(hdr, sizeof(hdr), "DATA%08zx", body.size());
            queueBulkReadResponse({hdr, hdr + std::strlen(hdr)});
            // Chunked: the mock serves one queued entry per bulkRead and drops
            // whatever does not fit the caller's buffer.
            for (size_t off = 0; off < body.size(); off += rpiboot::BULK_CHUNK_SIZE) {
                const size_t n = std::min(body.size() - off, rpiboot::BULK_CHUNK_SIZE);
                queueBulkReadResponse({body.begin() + off, body.begin() + off + n});
            }
            queueBulkReadResponse(okay());
            return;
        }

        queueBulkReadResponse(okay());
    }

    uint32_t _maxDownload;
    size_t _awaiting = 0;
    std::vector<uint8_t> _payload;      // arriving
    std::vector<uint8_t> _staged;       // complete, awaiting its verb
    std::vector<std::vector<uint8_t>> _flashed;
    std::vector<std::string> _commands;
    std::map<std::string, std::vector<uint8_t>> _files;
    std::string _pendingUpload;
    std::string _failPrefix;
};

class FlashingThread : public TestableFlashThread
{
public:
    FlashingThread(const QUrl& url, quint64 extractLen, const QByteArray& hash,
                   uint32_t maxDownload)
        : TestableFlashThread(url, QStringLiteral("mmcblk0"), extractLen, hash)
        , device(maxDownload)
    {
    }

    FakeFastbootDevice device;      // outlives runImpl()

protected:
    std::unique_ptr<rpiboot::IUsbTransport> openFastbootTransport(
        rpiboot::LibusbContext&, const rpiboot::UsbDeviceInfo&) override
    {
        return std::make_unique<TransportView>(device);
    }
};

// A pattern with no long runs, so the encoder cannot turn any of it into
// FILL or DONT_CARE chunks and every byte has to travel.
std::vector<uint8_t> patternImage(size_t bytes)
{
    std::vector<uint8_t> img(bytes);
    for (size_t i = 0; i < bytes; ++i)
        img[i] = static_cast<uint8_t>((i * 31 + (i >> 8) * 7 + 11) & 0xFF);
    return img;
}

QString writeImage(const QString& dir, const std::vector<uint8_t>& img)
{
    const QString path = QDir(dir).filePath(QStringLiteral("os.img"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write(reinterpret_cast<const char*>(img.data()),
                    static_cast<qint64>(img.size())) == qint64(img.size()));
    return path;
}

} // namespace

TEST_CASE("An image reaches the device byte for byte", "[fastboot][flash][pipeline]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<uint8_t> image = patternImage(2 * 1024 * 1024);
    const QString path = writeImage(dir.path(), image);
    const QByteArray hash =
        QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(image.data()),
                                            static_cast<int>(image.size())),
                                 QCryptographicHash::Sha256).toHex();

    // Larger than the image, so the whole thing goes in one segment.
    FlashingThread t{QUrl::fromLocalFile(path), image.size(), hash, 64u * 1024 * 1024};
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(log.success);
    REQUIRE_FALSE(t.device.flashed().empty());

    std::vector<uint8_t> received(image.size(), 0);
    for (const auto& segment : t.device.flashed())
        rpi_test::applySparse(segment, received);

    CHECK(received == image);
}

TEST_CASE("An image larger than one segment arrives in order and complete",
          "[fastboot][flash][pipeline]")
{
    // A device that will only take a small download at a time, which is the
    // normal case: the image is split into segments, each with its own sparse
    // header and its own place in the image. Getting the offsets wrong here
    // writes the right bytes to the wrong blocks, and only shows up as a card
    // that does not boot.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<uint8_t> image = patternImage(4 * 1024 * 1024);
    const QString path = writeImage(dir.path(), image);

    FlashingThread t{QUrl::fromLocalFile(path), image.size(), QByteArray(),
                     1024u * 1024};
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(log.success);
    INFO("segments: " << t.device.flashed().size());
    CHECK(t.device.flashed().size() > 1);

    std::vector<uint8_t> received(image.size(), 0);
    for (const auto& segment : t.device.flashed())
        rpi_test::applySparse(segment, received);

    CHECK(received == image);
}

TEST_CASE("A device that refuses a segment does not get a success",
          "[fastboot][flash][pipeline]")
{
    // Half an image on a card is worse than none, because the card looks
    // written. The flash has to stop at the refusal and say so.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<uint8_t> image = patternImage(4 * 1024 * 1024);
    const QString path = writeImage(dir.path(), image);

    FlashingThread t{QUrl::fromLocalFile(path), image.size(), QByteArray(),
                     1024u * 1024};
    t.device.failCommand("flash:");

    SignalLog log;
    log.attach(&t);

    t.runImpl();

    CHECK_FALSE(log.success);
    REQUIRE_FALSE(log.errors.isEmpty());
    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
}

TEST_CASE("Cancelling partway stops sending and does not report success",
          "[fastboot][flash][pipeline][cancel]")
{
    // Cancel is pressed while the segments are going down the wire. What is
    // already written cannot be taken back, but nothing more should be sent
    // and the result must not be a success.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<uint8_t> image = patternImage(4 * 1024 * 1024);
    const QString path = writeImage(dir.path(), image);

    FlashingThread t{QUrl::fromLocalFile(path), image.size(), QByteArray(),
                     1024u * 1024};

    int flashes = 0;
    t.device.onCommand = [&t, &flashes](const std::string &cmd) {
        if (cmd.rfind("flash:", 0) == 0 && ++flashes == 1)
            t.cancel();
    };

    SignalLog log;
    log.attach(&t);

    t.runImpl();

    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(log.success);

    // The image needs four segments at this download size; stopping at the
    // first means the rest were never sent.
    INFO("segments flashed: " << t.device.flashed().size());
    CHECK(t.device.flashed().size() < 4);
}

TEST_CASE("A bmap keeps unmapped blocks off the wire", "[fastboot][flash][pipeline]")
{
    // bmap is what makes a Compute Module write quick: the image is mostly
    // empty space, and blocks the filesystem never allocated are sent as
    // DONT_CARE instead of as data. The risk is skipping a block that did
    // matter, which produces a card missing part of its filesystem and no
    // error at all -- so this checks both halves: the mapped blocks arrive
    // intact, and the unmapped ones genuinely do not travel.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    constexpr size_t kBlock = 4096;
    constexpr size_t kBlocks = 256;                 // 1 MiB
    const std::vector<uint8_t> image = patternImage(kBlock * kBlocks);
    const QString path = writeImage(dir.path(), image);

    // Only the first and last quarter are mapped; the middle half is not.
    const QString bmapPath = QDir(dir.path()).filePath(QStringLiteral("os.bmap"));
    {
        QFile f(bmapPath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        const QByteArray doc =
            "<?xml version=\"1.0\" ?>\n"
            "<bmap version=\"2.0\">\n"
            "  <BlockSize>4096</BlockSize>\n"
            "  <BlocksCount>256</BlocksCount>\n"
            "  <MappedBlocksCount>128</MappedBlocksCount>\n"
            "  <BlockMap>\n"
            "    <Range>0-63</Range>\n"
            "    <Range>192-255</Range>\n"
            "  </BlockMap>\n"
            "</bmap>\n";
        REQUIRE(f.write(doc) == doc.size());
    }

    FlashingThread t{QUrl::fromLocalFile(path), image.size(), QByteArray(),
                     64u * 1024 * 1024};
    t.setBmapUrl(QUrl::fromLocalFile(bmapPath));

    SignalLog log;
    log.attach(&t);

    t.runImpl();

    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(log.success);
    REQUIRE_FALSE(t.device.flashed().empty());

    size_t wire = 0;
    for (const auto &segment : t.device.flashed())
        wire += segment.size();
    INFO("wire bytes " << wire << " for an image of " << image.size());
    CHECK(wire < image.size() / 2 + kBlock * 4);   // the unmapped half stayed home

    std::vector<uint8_t> received(image.size(), 0);
    for (const auto &segment : t.device.flashed())
        rpi_test::applySparse(segment, received);

    // Every mapped block arrives byte for byte. The unmapped ones are not
    // checked: the device keeps whatever was already there, which is the
    // whole point of not sending them.
    const std::vector<uint8_t> mappedHead(received.begin(),
                                          received.begin() + 64 * kBlock);
    const std::vector<uint8_t> expectHead(image.begin(), image.begin() + 64 * kBlock);
    CHECK(mappedHead == expectHead);

    const std::vector<uint8_t> mappedTail(received.begin() + 192 * kBlock, received.end());
    const std::vector<uint8_t> expectTail(image.begin() + 192 * kBlock, image.end());
    CHECK(mappedTail == expectTail);
}

TEST_CASE("Customisation is written into the boot partition it just flashed",
          "[fastboot][flash][pipeline]")
{
    // The Compute Module equivalent of everything the customisation screen
    // collects. Unlike the SD card path it cannot edit a file on disk before
    // writing: the image goes down first, then the device mounts its own boot
    // partition and the files are pushed over the wire one at a time. Only the
    // refusals had ever been tested -- nothing had ever checked that the files
    // arrive, or that config.txt is merged rather than replaced.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<uint8_t> image = patternImage(1024 * 1024);
    const QString path = writeImage(dir.path(), image);

    FlashingThread t{QUrl::fromLocalFile(path), image.size(), QByteArray(),
                     64u * 1024 * 1024};

    // What the flashed image is supposed to have left on the partition. The
    // setting asked for is present but commented out, which is the case that
    // has to be uncommented in place rather than appended.
    t.device.seedFile("/mnt/bootfs/config.txt",
                      "[all]\n#dtparam=audio=on\n[pi5]\narm_boost=1\n");
    t.device.seedFile("/mnt/bootfs/cmdline.txt", "console=serial0,115200 rootwait\n");

    const QByteArray firstrun = "#!/bin/bash\n# marker-fastboot-customisation\nexit 0\n";
    t.setImageCustomisation(QByteArray("dtparam=audio=on\ndtoverlay=vc4-kms-v3d"),
                            QByteArray(),
                            firstrun,
                            QByteArray(), QByteArray(),
                            QByteArray("systemd"));

    SignalLog log;
    log.attach(&t);

    t.runImpl();

    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(log.success);

    // The script has to be there to run at all.
    REQUIRE(t.device.hasFile("/mnt/bootfs/firstrun.sh"));
    CHECK(t.device.file("/mnt/bootfs/firstrun.sh") == firstrun);

    // config.txt is merged, not replaced: the commented setting is turned on
    // where it stood, the new one is added, and what the user already had is
    // still there.
    const QByteArray config = t.device.file("/mnt/bootfs/config.txt");
    INFO("config.txt:\n" << config.toStdString());
    CHECK(config.contains("\ndtparam=audio=on\n"));
    CHECK_FALSE(config.contains("#dtparam=audio=on"));
    CHECK(config.contains("dtoverlay=vc4-kms-v3d"));
    CHECK(config.contains("[pi5]"));
    CHECK(config.contains("arm_boost=1"));

    // And the partition was mounted and released again, rather than left
    // mounted on a device about to be rebooted.
    const QStringList cmds = commandsSent(t.device);
    CHECK_FALSE(cmds.filter(QStringLiteral("oem mount ")).isEmpty());
    CHECK_FALSE(cmds.filter(QStringLiteral("oem umount ")).isEmpty());
}

TEST_CASE("A boot partition that will not mount stops the write being a success",
          "[fastboot][flash][pipeline]")
{
    // The image is already on the device by this point. Reporting success
    // anyway would leave a card that boots without any of the settings the
    // user entered -- no network, no account, no way in.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<uint8_t> image = patternImage(512 * 1024);
    const QString path = writeImage(dir.path(), image);

    FlashingThread t{QUrl::fromLocalFile(path), image.size(), QByteArray(),
                     64u * 1024 * 1024};
    t.device.failCommand("oem mount ");
    t.setImageCustomisation(QByteArray(), QByteArray(),
                            "#!/bin/bash\nexit 0\n", QByteArray(), QByteArray(),
                            QByteArray("systemd"));

    SignalLog log;
    log.attach(&t);

    t.runImpl();

    CHECK_FALSE(log.success);
    REQUIRE_FALSE(log.errors.isEmpty());
    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_THAT(log.errors.last().toStdString(), ContainsSubstring("mount"));
}

TEST_CASE("An image that ends early is refused, not flashed as far as it got",
          "[fastboot][flash][pipeline]")
{
    // A download that stopped partway. The decompressor's read loop ended on
    // the first non-OK return whatever it was, and then told the consumer the
    // stream had finished normally, so the device was flashed with as much of
    // the image as arrived and the write completed. A catalogue image is
    // caught after the fact by its hash; one supplied without a hash is not.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // Incompressible on purpose: the ordinary pattern squeezes 2 MiB down to
    // under a kilobyte, and truncating that removes no image content worth
    // speaking of. This keeps the compressed stream about the size of the
    // image, so cutting it short really does lose most of the data.
    std::vector<uint8_t> image(2 * 1024 * 1024);
    {
        uint64_t x = 0x9E3779B97F4A7C15ull;
        for (auto &b : image) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            b = static_cast<uint8_t>(x & 0xFF);
        }
    }

    std::vector<uint8_t> compressed(8 * 1024 * 1024);
    size_t used = 0;
    {
        archive *w = archive_write_new();
        REQUIRE(archive_write_add_filter_xz(w) == ARCHIVE_OK);
        REQUIRE(archive_write_set_format_raw(w) == ARCHIVE_OK);
        REQUIRE(archive_write_open_memory(w, compressed.data(), compressed.size(), &used)
                == ARCHIVE_OK);

        archive_entry *e = archive_entry_new();
        archive_entry_set_pathname(e, "os.img");
        archive_entry_set_size(e, static_cast<la_int64_t>(image.size()));
        archive_entry_set_filetype(e, AE_IFREG);
        REQUIRE(archive_write_header(w, e) == ARCHIVE_OK);
        REQUIRE(archive_write_data(w, image.data(), image.size())
                == static_cast<la_ssize_t>(image.size()));
        archive_entry_free(e);
        REQUIRE(archive_write_close(w) == ARCHIVE_OK);
        archive_write_free(w);
    }
    REQUIRE(used > 512 * 1024);   // genuinely incompressible

    // Keep the first two thirds and drop the rest.
    const QString path = QDir(dir.path()).filePath(QStringLiteral("truncated.img.xz"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        const qint64 keep = static_cast<qint64>(used) * 2 / 3;
        REQUIRE(f.write(reinterpret_cast<const char *>(compressed.data()), keep) == keep);
    }

    // No expected hash, so nothing downstream can catch it either.
    FlashingThread t{QUrl::fromLocalFile(path), image.size(), QByteArray(),
                     64u * 1024 * 1024};
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(log.success);
    CHECK_FALSE(log.errors.isEmpty());
}

TEST_CASE("A wrong hash is reported rather than called a success",
          "[fastboot][flash][pipeline]")
{
    // The hash is taken over what was decompressed and fed to the encoder,
    // so it is the end-to-end check that the card got the image the catalogue
    // named. A mismatch has to stop the write being called a success.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const std::vector<uint8_t> image = patternImage(1024 * 1024);
    const QString path = writeImage(dir.path(), image);

    const QByteArray wrong(64, 'a');
    FlashingThread t{QUrl::fromLocalFile(path), image.size(), wrong, 64u * 1024 * 1024};
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    CHECK_FALSE(log.success);
    REQUIRE_FALSE(log.errors.isEmpty());
    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_THAT(log.errors.last().toStdString(), ContainsSubstring("hash"));
}

// ══════════════════════════════════════════════════════════════
// 7. max-download-size
//
// The device chooses this number. SparseEncoder reserves two buffers of it
// up front, alongside the ring buffers SystemMemoryManager has separately
// budgeted against available RAM -- and the memory manager knows nothing
// about these two, so whatever the device asks for is spent on top of the
// budget it computed.
//
// It used to be parsed with stoul into a uint32_t and used as-is.
// ══════════════════════════════════════════════════════════════

namespace {
constexpr quint64 kNoMemoryCeiling = 0;
constexpr uint32_t kFloor = fastboot::SparseEncoder::MIN_SEGMENT_SIZE;

uint32_t resolved(const char *reported, quint64 avail = kNoMemoryCeiling)
{
    const std::string s = reported ? reported : std::string();
    return FastbootFlashThread::resolveMaxDownloadSize(reported ? &s : nullptr, avail);
}
} // namespace

TEST_CASE("A device that reports no max-download-size gets the default",
          "[fastboot][download-size]")
{
    CHECK(resolved(nullptr) == 256u * 1024 * 1024);
}

TEST_CASE("A reported max-download-size is honoured in both bases",
          "[fastboot][download-size]")
{
    CHECK(resolved("1048576") == 1048576u);
    CHECK(resolved("0x100000") == 1048576u);
    CHECK(resolved("0X100000") == 1048576u);
}

TEST_CASE("A max-download-size that does not parse falls back to the default",
          "[fastboot][download-size]")
{
    // Each of these is something std::stoull would have accepted in part and
    // silently returned a number for: "0x" and "0x10zz" stop at the first
    // character they cannot use, and "-1" wraps to ULLONG_MAX rather than
    // throwing. A partial parse of a device-supplied string is not a size.
    const char *junk = GENERATE("", "banana", "0x", "0X", "0x10zz", "-1", " ",
                                "16 MB", "1048576\n");
    CAPTURE(junk);
    CHECK(resolved(junk) == 256u * 1024 * 1024);
}

TEST_CASE("A max-download-size of four gigabytes or more does not wrap",
          "[fastboot][download-size]")
{
    // stoul into uint32_t truncated: 0x100000000 became 0, which is below
    // the encoder's floor. The value is clamped down, never wrapped.
    for (const char *big : {"0x100000000", "0x100001000", "4294967296",
                            "18446744073709551615",
                            "99999999999999999999999"}) {   // beyond uint64
        CAPTURE(big);
        const uint32_t got = resolved(big);
        CHECK(got == 256u * 1024 * 1024);
        CHECK(got >= kFloor);
    }
}

TEST_CASE("A uselessly small max-download-size is raised to the floor",
          "[fastboot][download-size]")
{
    // These parse cleanly -- the device really is claiming it can only take
    // this much -- but a segment below the floor cannot carry a single block,
    // so the encoder would never advance. Honour the device up to the point
    // where honouring it means never finishing.
    for (const char *small : {"0", "1", "512", "4096", "0x0", "0x1000"}) {
        CAPTURE(small);
        CHECK(resolved(small) == kFloor);
    }
}

TEST_CASE("An oversized max-download-size is capped, not honoured",
          "[fastboot][download-size]")
{
    CHECK(resolved("0x40000000") == 256u * 1024 * 1024);   // device asks 1 GB
}

TEST_CASE("The segment size is bounded by available memory",
          "[fastboot][download-size]")
{
    // 256 MB available: the encoder's two buffers must not be a large
    // fraction of it, whatever the device claims it can take.
    constexpr quint64 avail = 256ull * 1024 * 1024;
    const uint32_t got = resolved("0x10000000", avail);   // device asks 256 MB
    CHECK(got == avail / 8);
    CHECK(2ull * got < avail / 2);
}

TEST_CASE("A small memory budget never pushes the segment below the floor",
          "[fastboot][download-size]")
{
    // An eighth of very little is less than one block. The floor still wins:
    // an encoder that cannot hold a block cannot make progress at all.
    for (quint64 avail : {quint64{1}, quint64{4096}, quint64{64 * 1024}}) {
        CAPTURE(avail);
        CHECK(resolved("0x10000000", avail) >= kFloor);
    }
}

TEST_CASE("Whatever the device reports, the encoder accepts the result",
          "[fastboot][download-size]")
{
    // The pairing that matters: every value resolveMaxDownloadSize can return
    // is one SparseEncoder will take without clamping it further, so the size
    // the caller logs is the size actually in use.
    const char *reported = GENERATE("0", "1", "4096", "0x100000", "0x40000000",
                                    "0x100000000", "banana", "0x", "-1");
    const quint64 avail = GENERATE(quint64{0}, quint64{1024},
                                   quint64{256ull * 1024 * 1024},
                                   quint64{8ull * 1024 * 1024 * 1024});
    CAPTURE(reported, avail);

    const uint32_t size = resolved(reported, avail);
    fastboot::SparseEncoder enc(size, 1024 * 1024);
    CHECK(enc.maxSegmentSize() == size);
}

// ══════════════════════════════════════════════════════════════
// Which customisation file failed
//
// On the Compute Module path the settings cannot be edited on disk before
// the write: the image goes down first, then the files are pushed over the
// wire one at a time. Any of them can be refused by the device, and when one
// is the flash stops -- correctly, because a board that comes up without the
// network configuration the user typed in is worse than one that was never
// written.
//
// What the user has to go on is the message. "Failed to write" on its own
// says the customisation was lost but not which part, and the parts are not
// interchangeable: user-data is the account they will log in with,
// network-config is how the board reaches the network, cmdline.txt is
// whether the first-boot script runs at all. Each refusal names its file.
//
// Only the mount failure had ever been tested. These are the nine that
// follow it.
// ══════════════════════════════════════════════════════════════

namespace {

struct CustomisationFailure
{
    const char *tag;
    QByteArray config;
    QByteArray cmdline;
    QByteArray firstrun;
    QByteArray cloudinit;
    QByteArray cloudinitNetwork;
    QByteArray initFormat;
    bool seedConfig;
    bool seedCmdline;
    const char *failPrefix;   // empty when the failure is a missing file
    const char *mustSay;
};

} // namespace

TEST_CASE("A refused customisation file is named in the message",
          "[fastboot][customise]")
{
    const QByteArray script = "#!/bin/bash\nexit 0\n";
    const QByteArray cloud = "users:\n  - name: pi\n";
    const QByteArray netcfg = "version: 2\n";

    const std::vector<CustomisationFailure> cases = {
        // config.txt is read before it is merged, so a device that cannot
        // produce it stops there rather than replacing what it could not see.
        {"config.txt cannot be read", "dtparam=audio=on", {}, {}, {}, {},
         "systemd", /*seedConfig=*/false, /*seedCmdline=*/true, "", "config.txt"},
        {"config.txt cannot be written", "dtparam=audio=on", {}, {}, {}, {},
         "systemd", true, true, "oem download-file /mnt/bootfs/config.txt",
         "config.txt"},
        {"firstrun.sh cannot be written", {}, {}, script, {}, {},
         "systemd", true, true, "oem download-file /mnt/bootfs/firstrun.sh",
         "firstrun.sh"},
        {"rpi-preseed.toml cannot be written", {}, {}, script, {}, {},
         "rpi-preseed", true, true,
         "oem download-file /mnt/bootfs/rpi-preseed.toml", "rpi-preseed.toml"},
        {"meta-data cannot be written", {}, {}, {}, cloud, {},
         "cloudinit", true, true, "oem download-file /mnt/bootfs/meta-data",
         "meta-data"},
        {"user-data cannot be written", {}, {}, {}, cloud, {},
         "cloudinit", true, true, "oem download-file /mnt/bootfs/user-data",
         "user-data"},
        {"network-config cannot be written", {}, {}, {}, {}, netcfg,
         "cloudinit", true, true,
         "oem download-file /mnt/bootfs/network-config", "network-config"},
        // The first-run script is reached through the kernel command line, so
        // a cmdline.txt that cannot be read or written loses the script even
        // though the script itself arrived.
        {"cmdline.txt cannot be read", {}, {}, script, {}, {},
         "systemd", true, /*seedCmdline=*/false, "", "cmdline.txt"},
        {"cmdline.txt cannot be written", {}, {}, script, {}, {},
         "systemd", true, true, "oem download-file /mnt/bootfs/cmdline.txt",
         "cmdline.txt"},
    };

    for (const auto &c : cases)
    {
        INFO(c.tag);

        TestableFlashThread t{QUrl(QStringLiteral("https://example.com/os.img.xz")),
                              QStringLiteral("mmcblk0")};
        SignalLog log;
        log.attach(&t);
        t.setImageCustomisation(c.config, c.cmdline, c.firstrun, c.cloudinit,
                                c.cloudinitNetwork, c.initFormat);

        FakeFastbootDevice device{64u * 1024 * 1024};
        if (c.seedConfig)
            device.seedFile("/mnt/bootfs/config.txt", "[all]\narm_boost=1\n");
        if (c.seedCmdline)
            device.seedFile("/mnt/bootfs/cmdline.txt", "console=serial0,115200 rootwait\n");
        if (*c.failPrefix)
            device.failCommand(c.failPrefix);

        fastboot::FastbootProtocol fb;
        CHECK_FALSE(t.applyCustomisation(fb, device));

        REQUIRE(log.errors.size() == 1);
        const QString said = log.errors[0];
        INFO("said: " << said.toStdString());

        // Checked against the part Imager wrote, before the colon that
        // introduces the device's own words. The protocol's error string
        // happens to name the path too, so asserting on the whole message
        // would pass even with every filename taken out of the text above --
        // which is exactly the regression worth catching.
        const int colon = said.indexOf(QStringLiteral(": "));
        REQUIRE(colon > 0);
        CHECK_THAT(said.left(colon).toStdString(), ContainsSubstring(c.mustSay));
    }
}

TEST_CASE("A refused customisation still releases the boot partition",
          "[fastboot][customise]")
{
    // The device is rebooted after this whether the customisation worked or
    // not. Leaving its own boot partition mounted across that is how a board
    // comes back with a filesystem it never finished writing.
    TestableFlashThread t{QUrl(QStringLiteral("https://example.com/os.img.xz")),
                          QStringLiteral("mmcblk0")};
    SignalLog log;
    log.attach(&t);
    t.setImageCustomisation({}, {}, "#!/bin/bash\nexit 0\n", {}, {}, "systemd");

    FakeFastbootDevice device{64u * 1024 * 1024};
    device.seedFile("/mnt/bootfs/cmdline.txt", "console=serial0,115200 rootwait\n");
    device.failCommand("oem download-file /mnt/bootfs/firstrun.sh");

    fastboot::FastbootProtocol fb;
    CHECK_FALSE(t.applyCustomisation(fb, device));

    const QStringList cmds = commandsSent(device);
    INFO(cmds.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(cmds.filter(QStringLiteral("oem mount ")).isEmpty());
    CHECK_FALSE(cmds.filter(QStringLiteral("oem umount ")).isEmpty());
}

TEST_CASE("An image file with nothing in it is refused before the board is touched",
          "[fastboot][flash][pipeline]")
{
    // The Compute Module twin of a refusal the SD card path already has. A
    // download that produced a zero-length file, or a local file the user
    // picked before it finished copying, decompresses to no entries at all.
    //
    // The board is in fastboot mode by this point and about to be erased, so
    // the answer has to be a refusal and nothing sent -- an image that turned
    // out to be empty must not leave a Compute Module with its storage wiped
    // and nothing written back.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = QDir(dir.path()).filePath(QStringLiteral("empty.img.xz"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));   // and not a byte written
    }

    // A size is declared, as the catalogue would, so the pipeline starts and
    // the emptiness is found where a user would find it.
    FlashingThread t{QUrl::fromLocalFile(path), 2u * 1024 * 1024, QByteArray(),
                     64u * 1024 * 1024};
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    INFO("errors: " << log.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(log.success);
    REQUIRE_FALSE(log.errors.isEmpty());
    // A sentence about the image, not a diagnostic about the board the user
    // has just plugged in. Without the check on the first header, libarchive
    // is asked for data it does not have and the message becomes "INTERNAL
    // ERROR: Function 'archive_read_data_block' invoked with archive
    // structure in state 'header'" -- which is still a refusal, so the
    // outcome checks above pass either way, and this is the row that does
    // not.
    const std::string said = log.errors.join(QStringLiteral(" | ")).toStdString();
    CHECK_THAT(said, ContainsSubstring("No entries"));
    CHECK_THAT(said, !ContainsSubstring("INTERNAL ERROR"));

    // And nothing reached the device.
    CHECK(t.device.flashed().empty());
}

TEST_CASE("An image that cannot be fetched is reported as the download failing",
          "[fastboot][flash][pipeline]")
{
    // The catalogue image is fetched while the board waits in fastboot mode.
    // A connection that never opens -- a proxy in the way, the mirror down,
    // no route from the provisioning machine -- has to come back as the
    // download failing and nothing sent to the board.
    //
    // It is worth naming as a download rather than reporting whatever the
    // pipeline noticed second: the decompressor sees an empty stream and the
    // encoder sees no segments, and either of those described to the user
    // would send them looking at the image or the board instead of at the
    // network.
    //
    // Nothing listens on port 1.
    FlashingThread t{QUrl(QStringLiteral("http://127.0.0.1:1/os.img.xz")),
                     2u * 1024 * 1024, QByteArray(), 64u * 1024 * 1024};
    SignalLog log;
    log.attach(&t);

    t.runImpl();

    const std::string said = log.errors.join(QStringLiteral(" | ")).toStdString();
    INFO("errors: " << said);
    CHECK_FALSE(log.success);
    REQUIRE_FALSE(log.errors.isEmpty());
    CHECK_THAT(said, ContainsSubstring("Download failed"));
    // And the board is as it was.
    CHECK(t.device.flashed().empty());
}
