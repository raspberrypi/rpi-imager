/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Unit tests for the pieeprom.bin parser/editor.
 *
 * These tests are designed to be runnable offline. They use a small
 * synthetic image for byte-level invariants, plus an optional real
 * pieeprom.bin fixture (from /lib/firmware/raspberrypi/bootloader-2712/
 * if available) for round-trip parsing on a representative image.
 *
 * Cross-check against `rpi-eeprom-config --config` and the real
 * fixture is split into a separate test case that skips if the tool
 * is not on PATH.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "fastboot/pieeprom.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace fastboot::pieeprom;

// ── Helpers ────────────────────────────────────────────────────────────

namespace {

void putBE32(std::vector<uint8_t>& buf, size_t off, uint32_t v)
{
    if (buf.size() < off + 4) buf.resize(off + 4);
    buf[off    ] = uint8_t((v >> 24) & 0xff);
    buf[off + 1] = uint8_t((v >> 16) & 0xff);
    buf[off + 2] = uint8_t((v >>  8) & 0xff);
    buf[off + 3] = uint8_t( v        & 0xff);
}

// Construct a minimal synthetic pieeprom-shaped buffer containing a
// single FILE_MAGIC bootconf.txt section, followed by a PAD_MAGIC
// section, padded out to `totalSize` with 0xff. The buffer is laid
// out exactly the same way rpi-eeprom-config would lay it out.
std::vector<uint8_t> makeSyntheticImage(const std::string& bootconfText,
                                        size_t totalSize = 64 * 1024)
{
    std::vector<uint8_t> img(totalSize, 0xff);

    size_t off = 0;
    // bootconf.txt FILE_MAGIC section
    const size_t contentLen = bootconfText.size();
    const uint32_t length   = static_cast<uint32_t>(contentLen + FILENAME_LEN + 4);

    putBE32(img, off + 0, FILE_MAGIC);
    putBE32(img, off + 4, length);
    // filename at +8, 12 bytes, NUL-padded
    std::memset(&img[off + 8], 0, FILENAME_LEN);
    std::memcpy(&img[off + 8], "bootconf.txt", std::strlen("bootconf.txt"));
    // reserved 4 bytes at +20 are zero per real images
    std::memset(&img[off + 20], 0, 4);
    // content at +24
    std::memcpy(&img[off + 24], bootconfText.data(), contentLen);

    size_t end = off + SECTION_HDR_LEN + length;
    while (end % 8 != 0) {
        img[end++] = 0xff;
    }

    // Fill from `end` onward with 0xff (already initialised).
    // No PAD section needed since bootconf.txt is the last section here.
    return img;
}

std::string locateRealFixture()
{
    // Prefer the bootloader-2712 (Pi 5) latest image because it is the
    // most complex (signed, larger, more sections). Fall back to
    // bootloader-2711 if that is the only thing available.
    const QStringList candidates = {
        QStringLiteral("/lib/firmware/raspberrypi/bootloader-2712/latest"),
        QStringLiteral("/lib/firmware/raspberrypi/bootloader-2712/default"),
        QStringLiteral("/lib/firmware/raspberrypi/bootloader-2711/latest"),
        QStringLiteral("/lib/firmware/raspberrypi/bootloader-2711/default"),
    };
    for (const auto& dir : candidates) {
        QDir d(dir);
        if (!d.exists()) continue;
        QStringList bins = d.entryList(QStringList() << "pieeprom-*.bin",
                                        QDir::Files, QDir::Name);
        if (bins.isEmpty()) continue;
        return d.filePath(bins.last()).toStdString();
    }
    return {};
}

std::vector<uint8_t> readFileBytes(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

} // namespace

// ── Synthetic-image tests (always run) ─────────────────────────────────

TEST_CASE("synthetic image parses bootconf.txt section", "[pieeprom][parse]")
{
    auto img = makeSyntheticImage("[all]\nBOOT_ORDER=0xf41\n");
    Image image(std::move(img));

    REQUIRE(image.parse().empty());
    REQUIRE(image.sections().size() >= 1);

    const Section* s = image.findFile(FILE_BOOTCONF_TXT);
    REQUIRE(s != nullptr);
    CHECK(s->magic == FILE_MAGIC);
    CHECK(s->offset == 0);
    CHECK(s->filename == "bootconf.txt");

    auto text = image.readBootConfText();
    REQUIRE(text.has_value());
    CHECK(*text == "[all]\nBOOT_ORDER=0xf41\n");
}

TEST_CASE("rewriting bootconf with identical content is byte-idempotent",
          "[pieeprom][writeFile][idempotent]")
{
    const std::string txt = "[all]\nBOOT_ORDER=0xf41\nFOO=1\n";
    auto img = makeSyntheticImage(txt);
    auto before = img;

    Image image(std::move(img));
    REQUIRE(image.parse().empty());

    auto err = image.writeBootConfText(txt);
    REQUIRE(err.empty());

    CHECK(image.bytes() == before);
}

TEST_CASE("rewriting bootconf shorter inserts PAD_MAGIC unless last section",
          "[pieeprom][writeFile][padding]")
{
    // Two FILE_MAGIC sections: pubkey.bin (small) then bootconf.txt.
    // We then shrink pubkey.bin and verify a PAD_MAGIC fills the gap
    // up to bootconf.txt.

    // Lay out a buffer by hand: pubkey at offset 0, bootconf at offset 4096.
    std::vector<uint8_t> img(64 * 1024, 0xff);

    // pubkey.bin: 200 bytes of content at offset 0
    const size_t pubLen = 200;
    putBE32(img, 0,  FILE_MAGIC);
    putBE32(img, 4,  static_cast<uint32_t>(pubLen + FILENAME_LEN + 4));
    std::memset(&img[8], 0, FILENAME_LEN);
    std::memcpy(&img[8], "pubkey.bin", std::strlen("pubkey.bin"));
    std::memset(&img[20], 0, 4);
    for (size_t i = 0; i < pubLen; ++i) img[24 + i] = uint8_t(i & 0xff);

    // PAD_MAGIC bridges from end of pubkey to offset 4096. Real
    // images always put a PAD section in such a gap; the parser stops
    // at raw 0xffffffff, so without this it would miss bootconf.txt.
    const size_t pubEnd = 24 + pubLen; // 224, already 8-aligned
    const size_t padPayload = 4096 - pubEnd - 8; // bytes after the pad header
    putBE32(img, pubEnd + 0, PAD_MAGIC);
    putBE32(img, pubEnd + 4, static_cast<uint32_t>(padPayload));

    // bootconf.txt at offset 4096
    const std::string bootText = "[all]\nBOOT_ORDER=0xf41\n";
    putBE32(img, 4096 + 0, FILE_MAGIC);
    putBE32(img, 4096 + 4, static_cast<uint32_t>(bootText.size() + FILENAME_LEN + 4));
    std::memset(&img[4096 + 8], 0, FILENAME_LEN);
    std::memcpy(&img[4096 + 8], "bootconf.txt", std::strlen("bootconf.txt"));
    std::memset(&img[4096 + 20], 0, 4);
    std::memcpy(&img[4096 + 24], bootText.data(), bootText.size());

    Image image(std::move(img));
    REQUIRE(image.parse().empty());
    REQUIRE(image.findFile("pubkey.bin") != nullptr);
    REQUIRE(image.findFile("bootconf.txt") != nullptr);

    // Replace pubkey.bin with a shorter (32-byte) payload.
    std::vector<uint8_t> tinyPub(32, 0xaa);
    auto err = image.writeFile("pubkey.bin",
                                std::span<const uint8_t>(tinyPub));
    REQUIRE(err.empty());

    // Re-parse and verify: pubkey.bin is now smaller, a PAD_MAGIC
    // section fills the gap, then bootconf.txt remains intact at 4096.
    auto sections = image.sections();
    REQUIRE(sections.size() >= 3);
    CHECK(sections[0].magic == FILE_MAGIC);
    CHECK(sections[0].filename == "pubkey.bin");
    CHECK(image.readFile("pubkey.bin").value() == tinyPub);

    bool foundPad = false;
    bool foundBoot = false;
    for (const auto& s : sections) {
        if (s.magic == PAD_MAGIC) foundPad = true;
        if (s.isFile() && s.filename == "bootconf.txt") {
            foundBoot = true;
            CHECK(s.offset == 4096); // bootconf.txt did not move
        }
    }
    CHECK(foundPad);
    CHECK(foundBoot);
    CHECK(image.readBootConfText().value() == bootText);
}

TEST_CASE("writeFile refuses content that would overflow next section",
          "[pieeprom][writeFile][negative]")
{
    auto img = makeSyntheticImage("[all]\n");
    Image image(std::move(img));
    REQUIRE(image.parse().empty());

    // 4076 bytes is the maximum allowed; 5000 should be rejected.
    std::vector<uint8_t> tooBig(5000, 'x');
    auto err = image.writeFile("bootconf.txt",
                                std::span<const uint8_t>(tooBig));
    CHECK_FALSE(err.empty());
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("too large"));
}

// ── BOOT_ORDER text/numeric helpers ────────────────────────────────────

TEST_CASE("parseBootOrder extracts hex value from bootconf.txt", "[pieeprom][boot_order]")
{
    CHECK(parseBootOrder("BOOT_ORDER=0xf461\n").value() == 0xf461u);
    CHECK(parseBootOrder("[all]\nBOOT_ORDER=0xf41\n").value() == 0xf41u);
    CHECK(parseBootOrder("# comment\nBOOT_ORDER=0x1\n").value() == 0x1u);
    CHECK_FALSE(parseBootOrder("NO_BOOT_ORDER_HERE=1\n").has_value());
    CHECK_FALSE(parseBootOrder("BOOT_ORDER=garbage\n").has_value());
}

TEST_CASE("setBootOrderLine replaces existing line in place", "[pieeprom][boot_order]")
{
    auto out = setBootOrderLine("[all]\nBOOT_ORDER=0xf41\nFOO=1\n", 0xf416u);
    CHECK(out == "[all]\nBOOT_ORDER=0xf416\nFOO=1\n");
}

TEST_CASE("setBootOrderLine appends when missing", "[pieeprom][boot_order]")
{
    auto out = setBootOrderLine("[all]\nFOO=1\n", 0xf1u);
    CHECK(out == "[all]\nFOO=1\nBOOT_ORDER=0xf1\n");
}

TEST_CASE("composeBootOrder puts chosen device LSB, keeps fallbacks, tops with restart",
          "[pieeprom][boot_order][compose]")
{
    // NVMe-first from a typical default 0xf461:
    //   nibbles LSB->MSB = 1,6,4,f
    //   strip restart and the new first nibble (6); remaining = 1,4
    //   rebuilt = 6 | 1<<4 | 4<<8 | f<<12 = 0xf416
    CHECK(composeBootOrder(0xf461u, BootSource::Nvme)   == 0xf416u);
    CHECK(composeBootOrder(0xf41u,  BootSource::SdCard) == 0xf41u);
    CHECK(composeBootOrder(0u,       BootSource::SdCard) == 0xf1u);
    CHECK(composeBootOrder(0u,       BootSource::Nvme)   == 0xf6u);
}

TEST_CASE("composeBootOrder adds chosen device when not in current order",
          "[pieeprom][boot_order][compose]")
{
    // Pi 5 default 0xf41 (SD -> USB -> restart) with NVMe selected:
    // NVMe is not in the order; it must be inserted at LSB, keeping
    // SD and USB as fallbacks.
    CHECK(composeBootOrder(0xf41u, BootSource::Nvme) == 0xf416u);

    // Network-only board (0xf2) with USB selected: USB inserted at
    // LSB, network preserved as fallback.
    CHECK(composeBootOrder(0xf2u, BootSource::UsbMsd) == 0xf24u);

    // SD-only (0xf1) with NVMe selected: NVMe at LSB, SD as fallback.
    CHECK(composeBootOrder(0xf1u, BootSource::Nvme) == 0xf16u);
}

// ── Real-image fixture tests (skip if not available) ───────────────────

TEST_CASE("real pieeprom fixture parses and survives idempotent rewrite",
          "[pieeprom][fixture]")
{
    const std::string path = locateRealFixture();
    if (path.empty()) {
        SKIP("no pieeprom-*.bin fixture under /lib/firmware/raspberrypi/bootloader-*/");
    }

    auto bytes = readFileBytes(path);
    Image image(bytes);
    REQUIRE(image.parse().empty());

    // Every real fixture should contain bootconf.txt.
    auto bootText = image.readBootConfText();
    REQUIRE(bootText.has_value());
    CHECK_THAT(*bootText, Catch::Matchers::ContainsSubstring("BOOT_ORDER"));

    // Idempotent rewrite must not change a single byte.
    const auto before = image.bytes();

    // Locate the bootconf.txt section for diagnostic info.
    const Section* bs = image.findFile(FILE_BOOTCONF_TXT);
    REQUIRE(bs != nullptr);
    INFO("bootconf.txt at offset 0x" << std::hex << bs->offset
         << " length 0x" << bs->length);

    auto err = image.writeBootConfText(*bootText);
    REQUIRE(err.empty());

    // Find the first byte that differs.
    const auto& after = image.bytes();
    REQUIRE(after.size() == before.size());
    size_t firstDiff = after.size();
    for (size_t i = 0; i < after.size(); ++i) {
        if (after[i] != before[i]) { firstDiff = i; break; }
    }
    INFO("first diff at 0x" << std::hex << firstDiff
         << " before=0x" << (firstDiff < before.size() ? int(before[firstDiff]) : -1)
         << " after=0x"  << (firstDiff < after.size()  ? int(after[firstDiff])  : -1));
    CHECK(firstDiff == after.size());
}

TEST_CASE("real pieeprom fixture: changing BOOT_ORDER round-trips through rpi-eeprom-config",
          "[pieeprom][fixture][crosscheck]")
{
    const std::string path = locateRealFixture();
    if (path.empty()) {
        SKIP("no pieeprom-*.bin fixture under /lib/firmware/raspberrypi/bootloader-*/");
    }
    QString tool = QStandardPaths::findExecutable("rpi-eeprom-config");
    if (tool.isEmpty()) {
        SKIP("rpi-eeprom-config not on PATH; install rpi-eeprom to enable cross-check");
    }

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    const QString ourOutPath = scratch.filePath("ours.bin");
    const QString refOutPath = scratch.filePath("ref.bin");
    const QString cfgPath    = scratch.filePath("bootconf.txt");

    auto bytes = readFileBytes(path);

    // Ours: parse, edit BOOT_ORDER -> 0xf416, write back.
    Image image(bytes);
    REQUIRE(image.parse().empty());
    auto current = image.readBootConfText().value();
    auto cur = parseBootOrder(current).value_or(0xf41u);
    auto next = composeBootOrder(cur, BootSource::Nvme);
    auto edited = setBootOrderLine(current, next);
    REQUIRE(image.writeBootConfText(edited).empty());

    {
        QFile of(ourOutPath);
        REQUIRE(of.open(QIODevice::WriteOnly));
        of.write(reinterpret_cast<const char*>(image.bytes().data()),
                 static_cast<qint64>(image.bytes().size()));
    }

    // Reference: write our edited bootconf.txt to a file, then call
    // `rpi-eeprom-config --config <file> --out <out> <fixture>`.
    {
        QFile cf(cfgPath);
        REQUIRE(cf.open(QIODevice::WriteOnly));
        cf.write(edited.data(), static_cast<qint64>(edited.size()));
    }

    QProcess p;
    p.start(tool, QStringList()
        << "--config" << cfgPath
        << "--out"    << refOutPath
        << QString::fromStdString(path));
    REQUIRE(p.waitForStarted(5000));
    REQUIRE(p.waitForFinished(30000));
    INFO("rpi-eeprom-config stderr: " << p.readAllStandardError().toStdString());
    REQUIRE(p.exitCode() == 0);

    auto ours = readFileBytes(ourOutPath.toStdString());
    auto ref  = readFileBytes(refOutPath.toStdString());

    REQUIRE(ours.size() == ref.size());

    // Compare byte-by-byte; on mismatch, report the first divergence
    // location for fast debugging.
    size_t firstDiff = ours.size();
    for (size_t i = 0; i < ours.size(); ++i) {
        if (ours[i] != ref[i]) { firstDiff = i; break; }
    }
    INFO("first divergence at offset 0x" << std::hex << firstDiff);
    CHECK(ours == ref);
}
