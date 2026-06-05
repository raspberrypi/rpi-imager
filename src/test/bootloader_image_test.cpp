/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Unit tests for BootloaderImage, focused on AB-capable EEPROM images
 * (the "bootsys" partition file) and the AB-aware bootcode handling.
 *
 * The core happy-path tests run against a *real* AB-capable pieeprom.bin
 * pulled from the upstream rpi-eeprom repo (the latest firmware-2712
 * image is AB-capable). They SKIP cleanly when offline / curl is absent.
 * Two edge cases (oversized-payload rejection, non-AB detection) use a
 * small synthetic image because a real image can't exercise them.
 */

#include <catch2/catch_test_macros.hpp>

#include "rpiboot/bootloader_image.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstring>
#include <vector>

using namespace rpiboot;

namespace {

// ── Real upstream AB image ──────────────────────────────────────────────

bool curlTo(const QString& url, const QString& outPath)
{
    QProcess p;
    p.start(QStringLiteral("curl"),
            QStringList() << "-sfL" << "--max-time" << "30"
                          << "-o" << outPath << url);
    if (!p.waitForStarted(3000))
        return false;                       // curl not installed
    if (!p.waitForFinished(40000)) {
        p.kill();
        return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0
        && QFile(outPath).size() > 0;
}

// Download the latest AB-capable firmware-2712 pieeprom.bin into `dir`.
// Returns the local path, or an empty string when offline / unavailable.
QString fetchLatestABImage(QTemporaryDir& dir)
{
    constexpr const char* kApi =
        "https://api.github.com/repos/raspberrypi/rpi-eeprom/contents/firmware-2712/latest";
    constexpr const char* kRawBase =
        "https://github.com/raspberrypi/rpi-eeprom/raw/refs/heads/master/firmware-2712/latest/";

    const QString listing = dir.filePath("listing.json");
    if (!curlTo(QString::fromUtf8(kApi), listing))
        return {};

    QFile lf(listing);
    if (!lf.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(lf.readAll());
    if (!doc.isArray())
        return {};

    // Collect pieeprom-*.bin entries; the dated names sort chronologically.
    QStringList names;
    for (const QJsonValue& v : doc.array()) {
        const QString name = v.toObject().value("name").toString();
        if (name.startsWith("pieeprom-") && name.endsWith(".bin"))
            names << name;
    }
    if (names.isEmpty())
        return {};
    names.sort();

    // Walk newest-first and take the first AB-capable image.
    for (auto it = names.crbegin(); it != names.crend(); ++it) {
        const QString local = dir.filePath(*it);
        if (!curlTo(QString::fromUtf8(kRawBase) + *it, local))
            continue;
        BootloaderImage img;
        if (img.load(local) && img.isABImage())
            return local;
    }
    return {};
}

// ── Synthetic image (edge cases only) ────────────────────────────────────

void putBE32(std::vector<uint8_t>& b, size_t off, uint32_t v)
{
    b[off + 0] = uint8_t((v >> 24) & 0xff);
    b[off + 1] = uint8_t((v >> 16) & 0xff);
    b[off + 2] = uint8_t((v >>  8) & 0xff);
    b[off + 3] = uint8_t( v        & 0xff);
}

constexpr uint32_t MAGIC      = 0x55aaf00f;
constexpr uint32_t PAD_MAGIC  = 0x55aafeef;
constexpr uint32_t FILE_MAGIC = 0x55aaf11f;
constexpr size_t   IMAGE_SIZE = 2 * 1024 * 1024;
constexpr size_t   READ_ONLY  = 64 * 1024;

size_t writeFileSection(std::vector<uint8_t>& img, size_t off,
                        const char* name, const QByteArray& content)
{
    const uint32_t length = uint32_t(content.size() + 12 + 4); // filename + meta + payload
    putBE32(img, off + 0, FILE_MAGIC);
    putBE32(img, off + 4, length);
    std::memset(&img[off + 8], 0, 12);
    std::memcpy(&img[off + 8], name, std::strlen(name));
    std::memset(&img[off + 20], 0, 4);
    std::memcpy(&img[off + 24], content.constData(), content.size());
    size_t end = off + 8 + length;
    while (end % 8 != 0) img[end++] = 0xff;
    return end;
}

std::vector<uint8_t> makeABImage(const QByteArray& bootsys)
{
    std::vector<uint8_t> img(IMAGE_SIZE, 0xff);

    // bootcode (MAGIC) at offset 0.
    const QByteArray bootcode(100, '\xAA');
    putBE32(img, 0, MAGIC);
    putBE32(img, 4, uint32_t(bootcode.size()));
    std::memcpy(&img[8], bootcode.constData(), bootcode.size());
    size_t off = 8 + bootcode.size();
    while (off % 8 != 0) img[off++] = 0xff;

    // PAD section up to the 64 KiB read-only boundary.
    putBE32(img, off, PAD_MAGIC);
    putBE32(img, off + 4, uint32_t(READ_ONLY - (off + 8)));
    off = READ_ONLY;

    // First partition: bootsys immediately followed by bootconf.txt, so
    // bootsys cannot grow past the next section.
    off = writeFileSection(img, off, "bootsys", bootsys);
    writeFileSection(img, off, "bootconf.txt", QByteArray("BOOT_ORDER=0xf1\n"));
    return img;
}

std::vector<uint8_t> makeNonABImage()
{
    std::vector<uint8_t> img(IMAGE_SIZE, 0xff);
    const QByteArray bootcode(64, '\xAB');
    putBE32(img, 0, MAGIC);
    putBE32(img, 4, uint32_t(bootcode.size()));
    std::memcpy(&img[8], bootcode.constData(), bootcode.size());
    // bootconf.txt at the 128 KiB boundary keeps the bootcode reserve intact.
    writeFileSection(img, 128 * 1024, "bootconf.txt", QByteArray("BOOT_ORDER=0xf1\n"));
    return img;
}

QString writeTemp(QTemporaryDir& dir, const std::vector<uint8_t>& img)
{
    const QString path = dir.filePath("synthetic.bin");
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(reinterpret_cast<const char*>(img.data()), qint64(img.size()));
    f.close();
    return path;
}

} // namespace

// ── Real upstream image: AB detection, bootsys extract + round-trip ──────

TEST_CASE("real upstream 2712 image is AB-capable and bootsys round-trips",
          "[bootloader_image][network]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = fetchLatestABImage(dir);
    if (path.isEmpty())
        SKIP("could not fetch an AB-capable firmware-2712 pieeprom.bin "
             "(offline, curl missing, or no AB image upstream)");

    BootloaderImage img;
    REQUIRE(img.load(path));
    REQUIRE(img.isABImage());

    // bootsys and bootcode must both be present and non-trivial.
    const QByteArray bootsys  = img.getFile(QStringLiteral("bootsys"));
    const QByteArray bootcode = img.getFile(QStringLiteral("bootcode.bin"));
    REQUIRE(bootsys.size()  > 1024);
    REQUIRE(bootcode.size() > 1024);

    // Counter-signing appends 532 bytes: len(4)+keynum(4)+version(4)+
    // sig(256)+pubkey(264). Stand in for signBootcode2712 so the test
    // needs no RSA key/openssl. The real value matters: it changes the
    // 8-byte-aligned padding gap that exposed the PAD-vs-0xff EOF bug.
    constexpr int kSignOverhead = 4 + 4 + 4 + 256 + 264;
    QByteArray signedBootsys = bootsys;
    signedBootsys.append(QByteArray(kSignOverhead, '\x5A'));
    REQUIRE(img.updateBootsys(signedBootsys));
    CHECK(img.getFile(QStringLiteral("bootsys")) == signedBootsys);

    QByteArray signedBootcode = bootcode;
    signedBootcode.append(QByteArray(kSignOverhead, '\x5A'));
    REQUIRE(img.updateBootcode(signedBootcode)); // AB images skip the 128 KiB reserve
    CHECK(img.getFile(QStringLiteral("bootcode.bin")) == signedBootcode);
    // Updating bootcode must not drop later sections (regression: an
    // exactly-8-byte padding gap previously produced a premature EOF).
    CHECK(img.isABImage());
    CHECK(img.getFile(QStringLiteral("bootsys")) == signedBootsys);

    // Persist and reload to confirm the section table is still valid.
    const QString out = dir.filePath("out.bin");
    REQUIRE(img.save(out));
    BootloaderImage reloaded;
    REQUIRE(reloaded.load(out));
    CHECK(reloaded.isABImage());
    CHECK(reloaded.getFile(QStringLiteral("bootsys"))      == signedBootsys);
    CHECK(reloaded.getFile(QStringLiteral("bootcode.bin")) == signedBootcode);
}

// ── Synthetic edge cases (always run, offline) ───────────────────────────

TEST_CASE("updateBootsys rejects a payload that won't fit the partition slot",
          "[bootloader_image]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    BootloaderImage img;
    REQUIRE(img.load(writeTemp(dir, makeABImage(QByteArray(2000, '\xCD')))));
    REQUIRE(img.isABImage());

    // bootsys is immediately followed by bootconf.txt; an oversized payload
    // must fail rather than overrun the next section.
    CHECK_FALSE(img.updateBootsys(QByteArray(1024 * 1024, '\x5A')));
    CHECK_FALSE(img.lastError().isEmpty());
}

TEST_CASE("non-AB image reports false and has no bootsys to update",
          "[bootloader_image]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    BootloaderImage img;
    REQUIRE(img.load(writeTemp(dir, makeNonABImage())));
    CHECK_FALSE(img.isABImage());
    CHECK_FALSE(img.updateBootsys(QByteArray(2000, '\x5A')));
}
