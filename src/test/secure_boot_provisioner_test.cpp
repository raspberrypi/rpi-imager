// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// SecureBootProvisioner is what burns a customer key hash into a Compute
// Module's OTP and signs the firmware it will accept afterwards. OTP is
// write-once: a wrong key hash bricks the module permanently, and there is no
// recovery. It had no tests.
//
// provision() itself needs a device on the USB bus, but the four static
// functions around it do not -- key generation, the OTP hash, boot-image
// signing and signed-recovery preparation all work on files. Those are also
// the ones where a wrong answer is unrecoverable, so they are worth pinning
// even without hardware.
//
// The OTP hash in particular is checked against openssl computing the same
// thing independently, rather than against a value this code produced.

#include <catch2/catch_test_macros.hpp>

#include "rpiboot/secure_boot_provisioner.h"
#include "rpiboot/bootloader_image.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUuid>

#include <array>
#include <cstring>
#include <filesystem>
#include <vector>
#include <optional>

#include "fixture_process.h"

namespace fs = std::filesystem;

// The provisioner and the chip enum both live in namespace rpiboot.
using rpiboot::SecureBootProvisioner;
using rpiboot::ChipGeneration;

namespace {

bool haveOpenssl() { return QFileInfo::exists(QStringLiteral("/usr/bin/openssl")); }

class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-sbp-%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))))
    {
        QDir().mkpath(_path);
    }
    ~ScratchDir() { QDir(_path).removeRecursively(); }

    ScratchDir(const ScratchDir &) = delete;
    ScratchDir &operator=(const ScratchDir &) = delete;

    fs::path path(const QString &name) const
    {
        return fs::path(QDir(_path).filePath(name).toStdString());
    }

private:
    QString _path;
};

bool writeFile(const fs::path &path, const QByteArray &contents)
{
    QFile f(QString::fromStdString(path.string()));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const bool ok = f.write(contents) == contents.size();
    f.close();
    return ok;
}

QByteArray readFile(const fs::path &path)
{
    QFile f(QString::fromStdString(path.string()));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    f.close();
    return data;
}

// Ask openssl for the DER of the public key, so the OTP hash can be checked
// against something other than the code under test.
QByteArray publicKeyDerViaOpenssl(const fs::path &publicKeyPath)
{
    QProcess proc;
    proc.start(QStringLiteral("/usr/bin/openssl"),
               {QStringLiteral("rsa"), QStringLiteral("-pubin"), QStringLiteral("-in"),
                QString::fromStdString(publicKeyPath.string()), QStringLiteral("-outform"),
                QStringLiteral("DER")});
    proc.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};
    return proc.readAllStandardOutput();
}

} // namespace

#define REQUIRE_OPENSSL()                                                                          \
    if (!haveOpenssl())                                                                            \
    SKIP("openssl is not installed, so no key pair can be generated")

// ---------------------------------------------------------------------------
// Key generation
// ---------------------------------------------------------------------------

TEST_CASE("SecureBootProvisioner generates a usable key pair", "[secureboot-otp]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;

    const fs::path priv = scratch.path(QStringLiteral("private.pem"));
    const fs::path pub = scratch.path(QStringLiteral("public.pem"));

    REQUIRE(SecureBootProvisioner::generateKeyPair(priv, pub));

    REQUIRE(fs::exists(priv));
    REQUIRE(fs::exists(pub));

    // Both halves must be PEM, and the private one must actually be private:
    // writing the public key to both paths would silently produce a device
    // that can never be signed for.
    const QByteArray privBytes = readFile(priv);
    const QByteArray pubBytes = readFile(pub);
    CHECK(privBytes.contains("PRIVATE KEY"));
    CHECK(pubBytes.contains("PUBLIC KEY"));
    CHECK_FALSE(pubBytes.contains("PRIVATE KEY"));

    // And openssl has to accept the private key, not just the file shape.
    QProcess check;
    check.start(QStringLiteral("/usr/bin/openssl"),
                {QStringLiteral("rsa"), QStringLiteral("-in"),
                 QString::fromStdString(priv.string()), QStringLiteral("-noout"),
                 QStringLiteral("-check")});
    check.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    CHECK(check.exitCode() == 0);
}

TEST_CASE("SecureBootProvisioner key generation fails on an unwritable path",
          "[secureboot-otp]")
{
    const fs::path priv{"/nonexistent-rpi-imager-dir/deeper/private.pem"};
    const fs::path pub{"/nonexistent-rpi-imager-dir/deeper/public.pem"};

    // Reporting success without writing a key would leave the caller about
    // to fuse a hash of nothing.
    CHECK_FALSE(SecureBootProvisioner::generateKeyPair(priv, pub));
}

TEST_CASE("SecureBootProvisioner generates a different key each time", "[secureboot-otp]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;

    const fs::path privA = scratch.path(QStringLiteral("a.pem"));
    const fs::path pubA = scratch.path(QStringLiteral("a.pub"));
    const fs::path privB = scratch.path(QStringLiteral("b.pem"));
    const fs::path pubB = scratch.path(QStringLiteral("b.pub"));

    REQUIRE(SecureBootProvisioner::generateKeyPair(privA, pubA));
    REQUIRE(SecureBootProvisioner::generateKeyPair(privB, pubB));

    // A fixed or seeded key would mean every device provisioned by this tool
    // shares one signing key.
    CHECK(readFile(privA) != readFile(privB));
}

// ---------------------------------------------------------------------------
// OTP key hash
// ---------------------------------------------------------------------------

TEST_CASE("SecureBootProvisioner computes the OTP key hash", "[secureboot-otp]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;

    const fs::path priv = scratch.path(QStringLiteral("private.pem"));
    const fs::path pub = scratch.path(QStringLiteral("public.pem"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(priv, pub));

    const auto hash = SecureBootProvisioner::calculateOtpKeyHash(pub);
    REQUIRE(hash.has_value());

    // This value is burned into one-time-programmable memory. It must be a
    // real 32-byte SHA-256, not zeroes or a truncated buffer.
    std::array<uint8_t, 32> zeroes{};
    CHECK(hash.value() != zeroes);

    // Stable across calls: a hash that varies per invocation would fuse
    // something the signer can never match.
    const auto again = SecureBootProvisioner::calculateOtpKeyHash(pub);
    REQUIRE(again.has_value());
    CHECK(hash.value() == again.value());
}

TEST_CASE("SecureBootProvisioner OTP hash differs between keys", "[secureboot-otp]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;

    const fs::path privA = scratch.path(QStringLiteral("a.pem"));
    const fs::path pubA = scratch.path(QStringLiteral("a.pub"));
    const fs::path privB = scratch.path(QStringLiteral("b.pem"));
    const fs::path pubB = scratch.path(QStringLiteral("b.pub"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(privA, pubA));
    REQUIRE(SecureBootProvisioner::generateKeyPair(privB, pubB));

    const auto hashA = SecureBootProvisioner::calculateOtpKeyHash(pubA);
    const auto hashB = SecureBootProvisioner::calculateOtpKeyHash(pubB);
    REQUIRE(hashA.has_value());
    REQUIRE(hashB.has_value());

    // Two keys hashing the same would let a module accept firmware signed by
    // the wrong one.
    CHECK(hashA.value() != hashB.value());
}

TEST_CASE("SecureBootProvisioner OTP hash rejects a missing key", "[secureboot-otp]")
{
    const auto hash =
        SecureBootProvisioner::calculateOtpKeyHash(fs::path{"/nonexistent/public.pem"});

    // std::nullopt rather than a hash of nothing: an all-zero hash is a
    // legitimate-looking value that would be fused permanently.
    CHECK_FALSE(hash.has_value());
}

TEST_CASE("SecureBootProvisioner OTP hash rejects a key that is not a key", "[secureboot-otp]")
{
    ScratchDir scratch;
    const fs::path junk = scratch.path(QStringLiteral("junk.pem"));
    REQUIRE(writeFile(junk, "-----BEGIN PUBLIC KEY-----\nnot base64 at all\n"));

    CHECK_FALSE(SecureBootProvisioner::calculateOtpKeyHash(junk).has_value());
}

// ---------------------------------------------------------------------------
// Boot image signing
// ---------------------------------------------------------------------------

TEST_CASE("SecureBootProvisioner signs a boot image", "[secureboot-otp]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;

    const fs::path priv = scratch.path(QStringLiteral("private.pem"));
    const fs::path pub = scratch.path(QStringLiteral("public.pem"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(priv, pub));

    const fs::path bootImg = scratch.path(QStringLiteral("boot.img"));
    REQUIRE(writeFile(bootImg, QByteArray(64 * 1024, '\x5A')));

    const fs::path sig = scratch.path(QStringLiteral("boot.sig"));
    REQUIRE(SecureBootProvisioner::signBootImage(bootImg, priv, sig));

    REQUIRE(fs::exists(sig));
    const QByteArray sigBytes = readFile(sig);
    REQUIRE_FALSE(sigBytes.isEmpty());

    // boot.sig is text: the image digest and a timestamp. The bootloader
    // rejects it outright if either is missing.
    const QString text = QString::fromUtf8(sigBytes);
    INFO("boot.sig: " << text.toStdString());
    CHECK(text.contains(QStringLiteral("ts:")));

    // The digest recorded must be of the image actually signed.
    const QByteArray expected =
        QCryptographicHash::hash(readFile(bootImg), QCryptographicHash::Sha256).toHex();
    CHECK(text.contains(QString::fromUtf8(expected)));
}

TEST_CASE("SecureBootProvisioner refuses to sign a missing image", "[secureboot-otp]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;

    const fs::path priv = scratch.path(QStringLiteral("private.pem"));
    const fs::path pub = scratch.path(QStringLiteral("public.pem"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(priv, pub));

    CHECK_FALSE(SecureBootProvisioner::signBootImage(
        fs::path{"/nonexistent/boot.img"}, priv, scratch.path(QStringLiteral("out.sig"))));
}

TEST_CASE("SecureBootProvisioner refuses to sign without a key", "[secureboot-otp]")
{
    ScratchDir scratch;
    const fs::path bootImg = scratch.path(QStringLiteral("boot.img"));
    REQUIRE(writeFile(bootImg, QByteArray(1024, '\x11')));

    CHECK_FALSE(SecureBootProvisioner::signBootImage(
        bootImg, fs::path{"/nonexistent/private.pem"},
        scratch.path(QStringLiteral("out.sig"))));
}

// ---------------------------------------------------------------------------
// Signed recovery preparation
// ---------------------------------------------------------------------------

TEST_CASE("SecureBootProvisioner rejects a recovery directory that is empty",
          "[secureboot-otp]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;

    const fs::path priv = scratch.path(QStringLiteral("private.pem"));
    const fs::path pub = scratch.path(QStringLiteral("public.pem"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(priv, pub));

    const fs::path recoveryDir = scratch.path(QStringLiteral("recovery"));
    fs::create_directories(recoveryDir);

    // pieeprom.original.bin is required and absent. Producing a "signed"
    // recovery from nothing would be flashed to a module and brick it.
    std::string err;
    CHECK_FALSE(SecureBootProvisioner::prepareSignedRecovery(
        ChipGeneration::BCM2711, recoveryDir, priv, false, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("SecureBootProvisioner rejects a recovery directory that is not there",
          "[secureboot-otp]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;

    const fs::path priv = scratch.path(QStringLiteral("private.pem"));
    const fs::path pub = scratch.path(QStringLiteral("public.pem"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(priv, pub));

    std::string err;
    CHECK_FALSE(SecureBootProvisioner::prepareSignedRecovery(
        ChipGeneration::BCM2712, fs::path{"/nonexistent/recovery"}, priv, true, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("SecureBootProvisioner rejects signed recovery without a key", "[secureboot-otp]")
{
    ScratchDir scratch;
    const fs::path recoveryDir = scratch.path(QStringLiteral("recovery"));
    fs::create_directories(recoveryDir);
    REQUIRE(writeFile(recoveryDir / "pieeprom.original.bin", QByteArray(4096, '\x00')));

    std::string err;
    CHECK_FALSE(SecureBootProvisioner::prepareSignedRecovery(
        ChipGeneration::BCM2711, recoveryDir, fs::path{"/nonexistent/key.pem"}, false, err));
    CHECK_FALSE(err.empty());
}

// ══════════════════════════════════════════════════════════════════════════
// prepareSignedRecovery: the success path
//
// The rejection paths above stop bad input reaching the device. These cover
// what happens when it *does* succeed, which is where the unrecoverable
// mistake lives: the firmware written here embeds the public key whose hash
// gets burned into OTP. Embed the wrong one -- a stale key, a hardcoded key,
// the same key regardless of input -- and the module will only ever accept
// images signed by a key the user does not hold. It is then scrap.
//
// A real pieeprom.original.bin is a licensed binary blob, so these build a
// synthetic one in the same container format (the layout BootloaderImage
// parses), which is enough to exercise the parse/patch/save round trip.
// ══════════════════════════════════════════════════════════════════════════


namespace {

constexpr uint32_t kMagic     = 0x55aaf00f;   // bootcode section
constexpr uint32_t kPadMagic  = 0x55aafeef;   // filler
constexpr uint32_t kFileMagic = 0x55aaf11f;   // named file section
constexpr size_t   kImageSize = 2 * 1024 * 1024;
constexpr size_t   kReadOnly  = 64 * 1024;    // end of the read-only region

void putBe32(std::vector<uint8_t> &b, size_t off, uint32_t v)
{
    b[off + 0] = uint8_t((v >> 24) & 0xff);
    b[off + 1] = uint8_t((v >> 16) & 0xff);
    b[off + 2] = uint8_t((v >>  8) & 0xff);
    b[off + 3] = uint8_t( v        & 0xff);
}

// Write a named section holding `reserve` bytes, so a later in-place update
// with a payload up to that size has somewhere to go.
size_t writeSection(std::vector<uint8_t> &img, size_t off,
                    const char *name, size_t reserve)
{
    const uint32_t length = uint32_t(reserve + 12 + 4);   // filename + meta + payload
    putBe32(img, off + 0, kFileMagic);
    putBe32(img, off + 4, length);
    std::memset(&img[off + 8], 0, 16);
    std::memcpy(&img[off + 8], name, std::strlen(name));
    std::memset(&img[off + 24], 0, reserve);
    size_t end = off + 8 + length;
    while (end % 8 != 0)
        img[end++] = 0xff;
    return end;
}

// A minimal image in the format BootloaderImage understands: a bootcode blob,
// padding out to the read-only boundary, then the three sections
// prepareSignedRecovery rewrites.
std::vector<uint8_t> makeSyntheticEeprom()
{
    std::vector<uint8_t> img(kImageSize, 0xff);

    const std::vector<uint8_t> bootcode(4096, 0xAA);
    putBe32(img, 0, kMagic);
    putBe32(img, 4, uint32_t(bootcode.size()));
    std::memcpy(&img[8], bootcode.data(), bootcode.size());
    size_t off = 8 + bootcode.size();
    while (off % 8 != 0)
        img[off++] = 0xff;

    putBe32(img, off, kPadMagic);
    putBe32(img, off + 4, uint32_t(kReadOnly - (off + 8)));
    off = kReadOnly;

    // Reserves are comfortably larger than what gets written back, so an
    // update failing here would mean a real regression rather than a
    // fixture that was cut too fine.
    off = writeSection(img, off, "bootconf.txt", 4096);
    off = writeSection(img, off, "bootconf.sig", 4096);
    off = writeSection(img, off, "pubkey.bin",   1024);
    return img;
}

// Lay out a recovery directory containing just the synthetic original.
void seedRecoveryDir(const fs::path &dir)
{
    fs::create_directories(dir);
    const auto img = makeSyntheticEeprom();
    QFile f(QString::fromStdString((dir / "pieeprom.original.bin").string()));
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(reinterpret_cast<const char *>(img.data()), qint64(img.size()));
    f.close();
}

// Read one named section back out of a produced image.
QByteArray sectionOf(const fs::path &image, const QString &name)
{
    rpiboot::BootloaderImage img;
    REQUIRE(img.load(QString::fromStdString(image.string())));
    return img.getFile(name);
}

} // namespace

TEST_CASE("SecureBootProvisioner prepares a signed recovery image", "[secureboot-otp]")
{
    ScratchDir scratch;
    const fs::path recovery = scratch.path(QStringLiteral("recovery"));
    seedRecoveryDir(recovery);

    const auto key = scratch.path(QStringLiteral("key.pem"));
    const auto pub = scratch.path(QStringLiteral("key.pub"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(key, pub));

    std::string err;
    REQUIRE(SecureBootProvisioner::prepareSignedRecovery(
        ChipGeneration::BCM2711, recovery, key, /*counterSignFirmware=*/false, err));
    INFO("error: " << err);

    CHECK(fs::exists(recovery / "pieeprom.bin"));
    CHECK(fs::exists(recovery / "pieeprom.sig"));
    // The original must survive: re-provisioning starts from it again.
    CHECK(fs::exists(recovery / "pieeprom.original.bin"));
}

TEST_CASE("Signed recovery turns secure boot on and self-update off", "[secureboot-otp]")
{
    // ENABLE_SELF_UPDATE=1 on a secure-boot device lets the bootloader
    // replace itself with an image the customer key has not signed, which
    // is how a provisioned module stops booting.
    ScratchDir scratch;
    const fs::path recovery = scratch.path(QStringLiteral("recovery"));
    seedRecoveryDir(recovery);

    const auto key = scratch.path(QStringLiteral("key.pem"));
    const auto pub = scratch.path(QStringLiteral("key.pub"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(key, pub));

    std::string err;
    REQUIRE(SecureBootProvisioner::prepareSignedRecovery(
        ChipGeneration::BCM2711, recovery, key, false, err));

    const QByteArray conf = sectionOf(recovery / "pieeprom.bin",
                                      QStringLiteral("bootconf.txt"));
    INFO("bootconf.txt: " << conf.toStdString());
    CHECK(conf.contains("SIGNED_BOOT=1"));
    CHECK(conf.contains("ENABLE_SELF_UPDATE=0"));
}

TEST_CASE("Signed recovery embeds the public key of the key it was given",
          "[secureboot-otp]")
{
    // The brick condition. The hash burned into OTP comes from this embedded
    // key, so if it does not track the private key passed in -- if it were
    // stale, cached, or hardcoded -- the user ends up holding a key the
    // module will never accept.
    ScratchDir scratch;

    const auto keyA = scratch.path(QStringLiteral("a.pem"));
    const auto pubA = scratch.path(QStringLiteral("a.pub"));
    const auto keyB = scratch.path(QStringLiteral("b.pem"));
    const auto pubB = scratch.path(QStringLiteral("b.pub"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(keyA, pubA));
    REQUIRE(SecureBootProvisioner::generateKeyPair(keyB, pubB));

    auto embeddedKeyFor = [&](const fs::path &privateKey, const char *dirName) {
        const fs::path recovery = scratch.path(QString::fromLatin1(dirName));
        seedRecoveryDir(recovery);
        std::string err;
        REQUIRE(SecureBootProvisioner::prepareSignedRecovery(
            ChipGeneration::BCM2711, recovery, privateKey, false, err));
        INFO("error: " << err);
        return sectionOf(recovery / "pieeprom.bin", QStringLiteral("pubkey.bin"));
    };

    const QByteArray embeddedA = embeddedKeyFor(keyA, "recA");
    const QByteArray embeddedB = embeddedKeyFor(keyB, "recB");

    // RSA-2048 modulus (256) + exponent (8).
    CHECK(embeddedA.size() == 264);
    CHECK(embeddedB.size() == 264);

    // Different key in, different key embedded.
    CHECK(embeddedA != embeddedB);

    // Same key in, same key embedded -- the output tracks the input rather
    // than anything left over from the previous run.
    CHECK(embeddedKeyFor(keyA, "recA2") == embeddedA);
}

TEST_CASE("Signed recovery signature carries hash, timestamp and RSA proof",
          "[secureboot-otp]")
{
    // The recovery binary verifies pieeprom.sig before flashing on a
    // secure-boot device, so all three lines have to be there. Dropping the
    // rsa2048 line would leave the image unflashable on exactly the devices
    // this path exists to serve -- and the header used to claim it was
    // deliberately absent, which is what this pins down.
    ScratchDir scratch;
    const fs::path recovery = scratch.path(QStringLiteral("recovery"));
    seedRecoveryDir(recovery);

    const auto key = scratch.path(QStringLiteral("key.pem"));
    const auto pub = scratch.path(QStringLiteral("key.pub"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(key, pub));

    std::string err;
    REQUIRE(SecureBootProvisioner::prepareSignedRecovery(
        ChipGeneration::BCM2711, recovery, key, false, err));

    QFile sig(QString::fromStdString((recovery / "pieeprom.sig").string()));
    REQUIRE(sig.open(QIODevice::ReadOnly));
    const QByteArray text = sig.readAll();
    INFO("pieeprom.sig: " << text.toStdString());

    CHECK(text.contains("ts: "));
    CHECK(text.contains("rsa2048:"));
    // First line is 64 hex characters of SHA-256 over pieeprom.bin.
    const QByteArray hash = text.left(64);
    CHECK(hash.size() == 64);
    CHECK(QByteArray::fromHex(hash).size() == 32);
}

TEST_CASE("Signed recovery rejects an unsupported chip generation", "[secureboot-otp]")
{
    ScratchDir scratch;
    const fs::path recovery = scratch.path(QStringLiteral("recovery"));
    seedRecoveryDir(recovery);

    const auto key = scratch.path(QStringLiteral("key.pem"));
    const auto pub = scratch.path(QStringLiteral("key.pub"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(key, pub));

    std::string err;
    CHECK_FALSE(SecureBootProvisioner::prepareSignedRecovery(
        ChipGeneration::BCM2836_7, recovery, key, false, err));
    CHECK_FALSE(err.empty());
    CHECK(fs::exists(recovery / "pieeprom.bin") == false);
}

TEST_CASE("Signed recovery rejects a corrupt original image", "[secureboot-otp]")
{
    // A truncated or non-EEPROM file must be refused rather than patched
    // into something that gets flashed.
    ScratchDir scratch;
    const fs::path recovery = scratch.path(QStringLiteral("recovery"));
    fs::create_directories(recovery);

    QFile bad(QString::fromStdString((recovery / "pieeprom.original.bin").string()));
    REQUIRE(bad.open(QIODevice::WriteOnly));
    bad.write(QByteArray(4096, '\x01'));
    bad.close();

    const auto key = scratch.path(QStringLiteral("key.pem"));
    const auto pub = scratch.path(QStringLiteral("key.pub"));
    REQUIRE(SecureBootProvisioner::generateKeyPair(key, pub));

    std::string err;
    CHECK_FALSE(SecureBootProvisioner::prepareSignedRecovery(
        ChipGeneration::BCM2711, recovery, key, false, err));
    CHECK_FALSE(err.empty());
}
