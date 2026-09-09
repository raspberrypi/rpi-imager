// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// SecureBoot assembles and signs the boot image that a locked-down Pi will
// accept. If it drops a file, mis-signs one, or reports success on a failure,
// the result is a device that either will not boot or boots something that
// was not checked. None of it was covered.
//
// Two things make it testable without hardware: the signing path shells out
// to openssl on Linux, so a throwaway RSA key is enough; and
// extractFatPartitionFiles() takes a DeviceWrapperFatPartition, which the FAT
// image fixture can now build from a scratch file.
//
// Cases that need openssl or mkfs.vfat skip themselves rather than fail, so
// this stays honest on a machine that has neither.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "secureboot.h"
#include "secureboot_crypto.h"
#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "file_operations.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#include <memory>
#include <stdexcept>

#include "fixture_process.h"
#include "platform_tools.h"
#include "platform_fat.h"

namespace {

bool haveTool(const QString &path)
{
    return QFileInfo::exists(path);
}

bool haveOpenssl() { return haveTool(QStringLiteral("/usr/bin/openssl")); }

// A scratch directory that removes itself.
class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-secureboot-%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))))
    {
        QDir().mkpath(_path);
    }
    ~ScratchDir() { QDir(_path).removeRecursively(); }

    ScratchDir(const ScratchDir &) = delete;
    ScratchDir &operator=(const ScratchDir &) = delete;

    QString filePath(const QString &name) const { return QDir(_path).filePath(name); }
    QString path() const { return _path; }

private:
    QString _path;
};

// Generate a throwaway 2048-bit RSA key. Nothing signs anything real here;
// the point is to exercise the signing path end to end.
bool generateRsaKey(const QString &path)
{
    QProcess openssl;
    openssl.start(QStringLiteral("/usr/bin/openssl"),
                  {QStringLiteral("genrsa"), QStringLiteral("-out"), path,
                   QStringLiteral("2048")});
    openssl.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    return openssl.exitStatus() == QProcess::NormalExit && openssl.exitCode() == 0 &&
           QFileInfo(path).size() > 0;
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(contents);
    f.close();
    return true;
}

} // namespace

#define REQUIRE_OPENSSL()                                                                          \
    if (!haveOpenssl())                                                                            \
    SKIP("openssl is not installed, so no key can be generated to sign with")

// ---------------------------------------------------------------------------
// Hashing and timestamps
// ---------------------------------------------------------------------------

TEST_CASE("SecureBoot hashes a file", "[secureboot]")
{
    ScratchDir scratch;
    const QString path = scratch.filePath(QStringLiteral("payload.bin"));
    const QByteArray contents = "the quick brown fox jumps over the lazy dog";
    REQUIRE(writeFile(path, contents));

    const QByteArray digest = SecureBoot::sha256File(path);

    // sha256File returns the digest hex-encoded, so 64 characters for the 32
    // raw bytes. Checked against Qt's own implementation rather than against
    // a value this code produced, so a change in either is caught.
    const QByteArray expected =
        QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
    REQUIRE(digest.size() == 64);
    CHECK(digest == expected);
}

TEST_CASE("SecureBoot hashes an empty file", "[secureboot]")
{
    ScratchDir scratch;
    const QString path = scratch.filePath(QStringLiteral("empty.bin"));
    REQUIRE(writeFile(path, QByteArray()));

    const QByteArray digest = SecureBoot::sha256File(path);
    CHECK(digest == QCryptographicHash::hash(QByteArray(), QCryptographicHash::Sha256).toHex());
    // The canonical SHA-256 of no bytes at all -- a useful independent check
    // that the hex encoding is not itself the thing being tested.
    CHECK(digest ==
          QByteArray("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST_CASE("SecureBoot hashes a file larger than one read", "[secureboot]")
{
    ScratchDir scratch;
    const QString path = scratch.filePath(QStringLiteral("big.bin"));

    QByteArray contents;
    contents.reserve(3 * 1024 * 1024);
    for (int i = 0; i < 3 * 1024 * 1024; ++i)
        contents.append(static_cast<char>(i % 251));
    REQUIRE(writeFile(path, contents));

    // Streams in chunks, so this covers the loop rather than the single-read
    // case above.
    CHECK(SecureBoot::sha256File(path) ==
          QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex());
}

TEST_CASE("SecureBoot reports a missing file rather than a hash of nothing", "[secureboot]")
{
    const QByteArray digest =
        SecureBoot::sha256File(QStringLiteral("/nonexistent-rpi-imager/no-such-file.bin"));

    // The dangerous outcome would be the digest of an empty buffer, which is
    // a real-looking hash of the wrong thing.
    CHECK(digest.isEmpty());
}

TEST_CASE("SecureBoot timestamp is a plausible epoch time", "[secureboot]")
{
    const qint64 now = SecureBoot::getCurrentTimestamp();

    // Somewhere after 2020 and before 2100 -- enough to catch a millisecond
    // or a zero being returned where seconds were meant.
    CHECK(now > 1577836800LL);
    CHECK(now < 4102444800LL);
}

// ---------------------------------------------------------------------------
// Signing
// ---------------------------------------------------------------------------

TEST_CASE("SecureBoot signs a digest with a real key", "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QByteArray digest =
        QCryptographicHash::hash(QByteArray("boot.img contents"), QCryptographicHash::Sha256);
    const QByteArray signature = SecureBoot::rsaSign(digest, key);

    // Returned hex-encoded, so 512 characters for the 256 raw bytes a
    // 2048-bit RSA signature always occupies.
    CHECK(signature.size() == 512);
    CHECK(QByteArray::fromHex(signature).size() == 256);
    CHECK_FALSE(QByteArray::fromHex(signature) == QByteArray(256, '\0'));
}

TEST_CASE("SecureBoot signing fails cleanly without a key", "[secureboot][crypto]")
{
    const QByteArray digest = QCryptographicHash::hash(QByteArray("x"), QCryptographicHash::Sha256);

    CHECK(SecureBoot::rsaSign(digest, QStringLiteral("/nonexistent/key.pem")).isEmpty());
}

TEST_CASE("SecureBoot signing fails cleanly on a key that is not a key", "[secureboot][crypto]")
{
    ScratchDir scratch;
    const QString notAKey = scratch.filePath(QStringLiteral("garbage.pem"));
    REQUIRE(writeFile(notAKey, "-----BEGIN RSA PRIVATE KEY-----\nnope\n"));

    const QByteArray digest = QCryptographicHash::hash(QByteArray("x"), QCryptographicHash::Sha256);
    CHECK(SecureBoot::rsaSign(digest, notAKey).isEmpty());
}

TEST_CASE("SecureBoot extracts a public key in binary form", "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QByteArray pubkey = SecureBoot::extractRsaPubkeyBin(key);

    // The bootloader wants the raw modulus and exponent, so this should be
    // around 256 bytes of modulus plus a little, not a PEM blob.
    CHECK(pubkey.size() >= 256);
    CHECK_FALSE(pubkey.startsWith("-----BEGIN"));
}

TEST_CASE("SecureBoot public key extraction fails cleanly", "[secureboot][crypto]")
{
    CHECK(SecureBoot::extractRsaPubkeyBin(QStringLiteral("/nonexistent/key.pem")).isEmpty());
}

TEST_CASE("SecureBoot generates a config signature", "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QByteArray config = "[all]\narm_64bit=1\n";
    const QByteArray sig = SecureBoot::generateConfigSig(config, key);

    REQUIRE_FALSE(sig.isEmpty());

    // The .sig file the bootloader reads is text: a hash line and a
    // timestamp line, not raw binary.
    const QString text = QString::fromUtf8(sig);
    INFO("config.sig contents: " << text.toStdString());
    CHECK(text.contains(QStringLiteral("ts:")));
}

TEST_CASE("SecureBoot config signature fails cleanly without a key", "[secureboot][crypto]")
{
    CHECK(SecureBoot::generateConfigSig("[all]\n", QStringLiteral("/nonexistent/key.pem"))
              .isEmpty());
}

TEST_CASE("SecureBoot signs a 2712 bootcode", "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    QByteArray bootcode(64 * 1024, '\xAB');
    const QByteArray signed_ = SecureBoot::signBootcode2712(bootcode, key);

    // The signed form carries the original plus a trailer, so it must be
    // strictly larger and must still start with the payload.
    CHECK(signed_.size() > bootcode.size());
    CHECK(signed_.startsWith(bootcode));
}

TEST_CASE("SecureBoot bootcode signing fails cleanly without a key", "[secureboot][crypto]")
{
    QByteArray bootcode(1024, '\x00');
    CHECK(SecureBoot::signBootcode2712(bootcode, QStringLiteral("/nonexistent/key.pem")).isEmpty());
}

// ---------------------------------------------------------------------------
// boot.img assembly
// ---------------------------------------------------------------------------

TEST_CASE("SecureBoot builds a boot.img from a file map", "[secureboot]")
{
    ScratchDir scratch;
    const QString out = scratch.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    files.insert(QStringLiteral("cmdline.txt"), "console=serial0,115200\n");
    files.insert(QStringLiteral("start4.elf"), QByteArray(4096, '\x11'));

    REQUIRE(SecureBoot::createBootImg(files, out));

    QFileInfo info(out);
    CHECK(info.exists());
    // A FAT image big enough to hold all three, so comfortably larger than
    // the sum of their contents.
    CHECK(info.size() > 4096);
}

TEST_CASE("SecureBoot builds a boot.img from an empty map", "[secureboot]")
{
    ScratchDir scratch;
    const QString out = scratch.filePath(QStringLiteral("empty-boot.img"));

    // Degenerate but legal: nothing to add, so either a valid empty image or
    // a clean refusal, never a crash or a truncated file.
    const bool ok = SecureBoot::createBootImg({}, out);
    if (ok)
        CHECK(QFileInfo(out).exists());
}

TEST_CASE("SecureBoot refuses to write a boot.img it cannot open", "[secureboot]")
{
    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");

    CHECK_FALSE(SecureBoot::createBootImg(
        files, QStringLiteral("/nonexistent-rpi-imager-dir/deeper/boot.img")));
}

TEST_CASE("SecureBoot signs a boot.img it just built", "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    const QString img = scratch.filePath(QStringLiteral("boot.img"));
    const QString sig = scratch.filePath(QStringLiteral("boot.sig"));
    REQUIRE(generateRsaKey(key));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    REQUIRE(SecureBoot::createBootImg(files, img));

    REQUIRE(SecureBoot::generateBootSig(img, key, sig));

    QFile f(sig);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QString contents = QString::fromUtf8(f.readAll());
    f.close();

    // boot.sig pairs the image digest with a timestamp; the bootloader
    // rejects it outright if either is missing.
    INFO("boot.sig contents: " << contents.toStdString());
    CHECK_FALSE(contents.isEmpty());
    CHECK(contents.contains(QStringLiteral("ts:")));
}

TEST_CASE("SecureBoot boot signature fails on a missing image", "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    CHECK_FALSE(SecureBoot::generateBootSig(QStringLiteral("/nonexistent/boot.img"), key,
                                            scratch.filePath(QStringLiteral("boot.sig"))));
}

TEST_CASE("SecureBoot boot signature fails on a missing key", "[secureboot][crypto]")
{
    ScratchDir scratch;
    const QString img = scratch.filePath(QStringLiteral("boot.img"));
    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    REQUIRE(SecureBoot::createBootImg(files, img));

    CHECK_FALSE(SecureBoot::generateBootSig(img, QStringLiteral("/nonexistent/key.pem"),
                                            scratch.filePath(QStringLiteral("boot.sig"))));
}

// ---------------------------------------------------------------------------
// Reading a boot partition
// ---------------------------------------------------------------------------

TEST_CASE("SecureBoot extracts every file from a boot partition", "[secureboot][fat]")
{
    if (!rpi_test::haveFatFormatter())
        SKIP(rpi_test::noFatFormatterReason());

    ScratchDir scratch;
    const QString imgPath = scratch.filePath(QStringLiteral("bootfs.img"));

    auto ops = rpi_imager::FileOperations::Create();
    REQUIRE(ops->CreateTestFile(imgPath.toStdString(), 64ull * 1024 * 1024) ==
            rpi_imager::FileError::kSuccess);

    QString formatError;
    INFO("formatter: " << formatError.toStdString());
    REQUIRE(rpi_test::makeFatFilesystem(imgPath, 32, QStringLiteral("bootfs"), &formatError));

    auto reopened = rpi_imager::FileOperations::Create();
    REQUIRE(reopened->OpenDevice(imgPath.toStdString()) == rpi_imager::FileError::kSuccess);

    DeviceWrapper dw(reopened.get());
    DeviceWrapperFatPartition fat(&dw, 0, 64ull * 1024 * 1024);

    // Names deliberately chosen to have 8.3 bases shorter than eight
    // characters: these are the ones the listing used to truncate, which made
    // them vanish from the extracted set entirely.
    fat.writeFile(QStringLiteral("config.txt"), "arm_64bit=1\n");
    fat.writeFile(QStringLiteral("start.elf"), QByteArray(2048, '\x22'));
    fat.writeFile(QStringLiteral("fixup.dat"), QByteArray(1024, '\x33'));
    dw.sync();

    const QMap<QString, QByteArray> extracted = SecureBoot::extractFatPartitionFiles(&fat);

    INFO("extracted: " << QStringList(extracted.keys()).join(QStringLiteral(", ")).toStdString());
    CHECK(extracted.size() >= 3);

    // Every extracted name must carry its contents, not an empty placeholder.
    for (auto it = extracted.constBegin(); it != extracted.constEnd(); ++it) {
        INFO("file: " << it.key().toStdString());
        CHECK_FALSE(it.value().isEmpty());
    }
}

// ══════════════════════════════════════════════════════════════
// The exact shape of boot.sig
//
// boot.sig is what a fused board checks before it will run the image beside
// it. Three lines, in order: the image's SHA-256 in hex, a timestamp, and
// the RSA signature. A board that rejects it does not boot, and a board
// with its OTP already programmed does not get a second chance -- so the
// format is worth holding to more tightly than "contains ts:".
//
// The signature is verified here with openssl's own dgst -verify rather
// than by re-signing and comparing. That is the same check any standard
// verifier performs, so passing it means the boot ROM would accept it too.
// ══════════════════════════════════════════════════════════════

namespace {

QStringList bootSigLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    return text.split(QChar('\n'), Qt::SkipEmptyParts);
}

// sha256sum via openssl, so the expectation is not computed by the same
// code being tested.
QByteArray opensslSha256Hex(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral("/usr/bin/openssl"),
            {QStringLiteral("dgst"), QStringLiteral("-sha256"),
             QStringLiteral("-hex"), path});
    if (!p.waitForFinished(rpi_test::kFixtureProcessTimeoutMs))
        return {};
    const QByteArray out = p.readAllStandardOutput().trimmed();
    const int eq = out.lastIndexOf('=');
    return eq >= 0 ? out.mid(eq + 1).trimmed() : out;
}

bool opensslVerify(const QString &keyPath, const QString &imgPath,
                   const QByteArray &signatureHex, const QString &scratch)
{
    const QString pub = scratch + QStringLiteral("/pub.pem");
    QProcess extract;
    extract.start(QStringLiteral("/usr/bin/openssl"),
                  {QStringLiteral("rsa"), QStringLiteral("-in"), keyPath,
                   QStringLiteral("-pubout"), QStringLiteral("-out"), pub});
    if (!extract.waitForFinished(rpi_test::kFixtureProcessTimeoutMs)
        || extract.exitCode() != 0)
        return false;

    const QString sigBin = scratch + QStringLiteral("/sig.bin");
    QFile sf(sigBin);
    if (!sf.open(QIODevice::WriteOnly))
        return false;
    sf.write(QByteArray::fromHex(signatureHex));
    sf.close();

    QProcess verify;
    verify.start(QStringLiteral("/usr/bin/openssl"),
                 {QStringLiteral("dgst"), QStringLiteral("-sha256"),
                  QStringLiteral("-verify"), pub,
                  QStringLiteral("-signature"), sigBin, imgPath});
    if (!verify.waitForFinished(rpi_test::kFixtureProcessTimeoutMs))
        return false;
    return verify.exitCode() == 0;
}

} // namespace

TEST_CASE("boot.sig has the three lines the bootloader reads",
          "[secureboot][crypto][bootsig]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    const QString img = scratch.filePath(QStringLiteral("boot.img"));
    const QString sig = scratch.filePath(QStringLiteral("boot.sig"));
    REQUIRE(generateRsaKey(key));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    REQUIRE(SecureBoot::createBootImg(files, img));
    REQUIRE(SecureBoot::generateBootSig(img, key, sig));

    const QStringList lines = bootSigLines(sig);
    INFO("boot.sig:\n" << lines.join(QChar('\n')).toStdString());
    REQUIRE(lines.size() == 3);

    CHECK(lines[0].size() == 64);
    CHECK(QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(lines[0]).hasMatch());
    CHECK(QRegularExpression(QStringLiteral("^ts: [0-9]+$")).match(lines[1]).hasMatch());
    CHECK(lines[2].startsWith(QStringLiteral("rsa2048: ")));
}

TEST_CASE("The digest in boot.sig is the digest of the image",
          "[secureboot][crypto][bootsig]")
{
    // Checked against openssl rather than against our own hasher, so this
    // does not merely confirm the code agrees with itself.
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    const QString img = scratch.filePath(QStringLiteral("boot.img"));
    const QString sig = scratch.filePath(QStringLiteral("boot.sig"));
    REQUIRE(generateRsaKey(key));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    REQUIRE(SecureBoot::createBootImg(files, img));
    REQUIRE(SecureBoot::generateBootSig(img, key, sig));

    const QStringList lines = bootSigLines(sig);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].toUtf8() == opensslSha256Hex(img));
}

TEST_CASE("The signature in boot.sig verifies against the key",
          "[secureboot][crypto][bootsig]")
{
    // The one that matters. openssl dgst -verify is what any standard
    // verifier does, so a signature that passes here is one the boot ROM
    // will accept -- and one that fails is a board that will not start.
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    const QString img = scratch.filePath(QStringLiteral("boot.img"));
    const QString sig = scratch.filePath(QStringLiteral("boot.sig"));
    REQUIRE(generateRsaKey(key));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    REQUIRE(SecureBoot::createBootImg(files, img));
    REQUIRE(SecureBoot::generateBootSig(img, key, sig));

    const QStringList lines = bootSigLines(sig);
    REQUIRE(lines.size() == 3);
    const QByteArray sigHex =
        lines[2].mid(QStringLiteral("rsa2048: ").size()).toUtf8();

    CHECK(opensslVerify(key, img, sigHex, scratch.path()));
}

TEST_CASE("A different image gets a different signature",
          "[secureboot][crypto][bootsig]")
{
    // A signature that did not depend on the image would verify against
    // anything, which is the whole failure secure boot exists to prevent.
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QString imgA = scratch.filePath(QStringLiteral("a.img"));
    const QString imgB = scratch.filePath(QStringLiteral("b.img"));
    const QString sigA = scratch.filePath(QStringLiteral("a.sig"));
    const QString sigB = scratch.filePath(QStringLiteral("b.sig"));

    QMap<QString, QByteArray> a, b;
    a.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    b.insert(QStringLiteral("config.txt"), "arm_64bit=0\n");
    REQUIRE(SecureBoot::createBootImg(a, imgA));
    REQUIRE(SecureBoot::createBootImg(b, imgB));
    REQUIRE(SecureBoot::generateBootSig(imgA, key, sigA));
    REQUIRE(SecureBoot::generateBootSig(imgB, key, sigB));

    const QStringList la = bootSigLines(sigA);
    const QStringList lb = bootSigLines(sigB);
    REQUIRE(la.size() == 3);
    REQUIRE(lb.size() == 3);

    CHECK(la[0] != lb[0]);          // different digest
    CHECK(la[2] != lb[2]);          // and so a different signature
}

TEST_CASE("A signature made for one image does not verify another",
          "[secureboot][crypto][bootsig]")
{
    // The substitution the bootloader is checking for: a genuine signature
    // moved onto an image it was not made for.
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QString imgA = scratch.filePath(QStringLiteral("a.img"));
    const QString imgB = scratch.filePath(QStringLiteral("b.img"));
    const QString sigA = scratch.filePath(QStringLiteral("a.sig"));

    QMap<QString, QByteArray> a, b;
    a.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    b.insert(QStringLiteral("config.txt"), "tampered\n");
    REQUIRE(SecureBoot::createBootImg(a, imgA));
    REQUIRE(SecureBoot::createBootImg(b, imgB));
    REQUIRE(SecureBoot::generateBootSig(imgA, key, sigA));

    const QStringList la = bootSigLines(sigA);
    REQUIRE(la.size() == 3);
    const QByteArray sigHex = la[2].mid(QStringLiteral("rsa2048: ").size()).toUtf8();

    CHECK(opensslVerify(key, imgA, sigHex, scratch.path()));
    CHECK_FALSE(opensslVerify(key, imgB, sigHex, scratch.path()));
}

TEST_CASE("The timestamp in boot.sig is a plausible time",
          "[secureboot][crypto][bootsig]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    const QString img = scratch.filePath(QStringLiteral("boot.img"));
    const QString sig = scratch.filePath(QStringLiteral("boot.sig"));
    REQUIRE(generateRsaKey(key));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "x\n");
    REQUIRE(SecureBoot::createBootImg(files, img));
    REQUIRE(SecureBoot::generateBootSig(img, key, sig));

    const QStringList lines = bootSigLines(sig);
    REQUIRE(lines.size() == 3);

    bool ok = false;
    const qint64 ts = lines[1].mid(4).toLongLong(&ok);
    REQUIRE(ok);
    CHECK(ts > 1600000000);                       // after 2020
    CHECK(ts < QDateTime::currentSecsSinceEpoch() + 60);
}

// ── The same three lines, for config.txt ────────────────────────────────
//
// generateConfigSig() signs the config text a secure-boot board reads at
// boot. Its output has the same shape as boot.sig and was asserted just as
// loosely -- non-empty and containing "ts:". A board that rejects this
// signature will not honour its own configuration; one that accepts a wrong
// one is the failure secure boot exists to stop.

namespace {

QStringList sigLines(const QByteArray &sig)
{
    return QString::fromUtf8(sig).split(QChar('\n'), Qt::SkipEmptyParts);
}

} // namespace

TEST_CASE("A config signature has the three lines the bootloader reads",
          "[secureboot][crypto][configsig]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QByteArray config = "[all]\narm_64bit=1\n";
    const QStringList lines = sigLines(SecureBoot::generateConfigSig(config, key));

    INFO("config.sig:\n" << lines.join(QChar('\n')).toStdString());
    REQUIRE(lines.size() == 3);
    CHECK(QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(lines[0]).hasMatch());
    CHECK(QRegularExpression(QStringLiteral("^ts: [0-9]+$")).match(lines[1]).hasMatch());
    CHECK(lines[2].startsWith(QStringLiteral("rsa2048: ")));
}

TEST_CASE("The config digest is of the config text exactly",
          "[secureboot][crypto][configsig]")
{
    // Signed over the raw bytes given, with nothing appended or trimmed --
    // a board hashes what is on the card, so any difference here is a
    // signature that will not match.
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QByteArray config = "[all]\narm_64bit=1\n";
    const QString configFile = scratch.filePath(QStringLiteral("config.txt"));
    QFile cf(configFile);
    REQUIRE(cf.open(QIODevice::WriteOnly));
    cf.write(config);
    cf.close();

    const QStringList lines = sigLines(SecureBoot::generateConfigSig(config, key));
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].toUtf8() == opensslSha256Hex(configFile));
}

TEST_CASE("The config signature verifies against the key",
          "[secureboot][crypto][configsig]")
{
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QByteArray config = "[all]\narm_64bit=1\n";
    const QString configFile = scratch.filePath(QStringLiteral("config.txt"));
    QFile cf(configFile);
    REQUIRE(cf.open(QIODevice::WriteOnly));
    cf.write(config);
    cf.close();

    const QStringList lines = sigLines(SecureBoot::generateConfigSig(config, key));
    REQUIRE(lines.size() == 3);
    const QByteArray sigHex = lines[2].mid(QStringLiteral("rsa2048: ").size()).toUtf8();

    CHECK(opensslVerify(key, configFile, sigHex, scratch.path()));
}

TEST_CASE("A changed config does not keep its old signature",
          "[secureboot][crypto][configsig]")
{
    // Editing config.txt on the card without re-signing has to stop the
    // board booting. If the old signature still verified, the file would
    // not be protected at all.
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QByteArray original = "[all]\narm_64bit=1\n";
    const QByteArray edited   = "[all]\narm_64bit=0\n";

    const QString editedFile = scratch.filePath(QStringLiteral("edited.txt"));
    QFile ef(editedFile);
    REQUIRE(ef.open(QIODevice::WriteOnly));
    ef.write(edited);
    ef.close();

    const QStringList before = sigLines(SecureBoot::generateConfigSig(original, key));
    const QStringList after  = sigLines(SecureBoot::generateConfigSig(edited, key));
    REQUIRE(before.size() == 3);
    REQUIRE(after.size() == 3);

    CHECK(before[0] != after[0]);
    CHECK(before[2] != after[2]);

    // And the original signature does not cover the edited file.
    const QByteArray originalSig =
        before[2].mid(QStringLiteral("rsa2048: ").size()).toUtf8();
    CHECK_FALSE(opensslVerify(key, editedFile, originalSig, scratch.path()));
}

TEST_CASE("An empty config still signs", "[secureboot][crypto][configsig]")
{
    // A board with no configuration is a legitimate state; refusing to sign
    // it would leave it unbootable for want of a file it does not need.
    REQUIRE_OPENSSL();
    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("private.pem"));
    REQUIRE(generateRsaKey(key));

    const QStringList lines = sigLines(SecureBoot::generateConfigSig(QByteArray(), key));
    REQUIRE(lines.size() == 3);
    // SHA-256 of the empty string.
    CHECK(lines[0] == QStringLiteral(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

// ── What is actually inside the boot.img ────────────────────────────────
//
// The case above builds an image from three files and checks it exists and
// is larger than 4096 bytes. An implementation that formatted an empty FAT
// volume and copied nothing into it would pass that -- and one did: the
// directory-ordering bug fixed in bootimgcreator_linux.cpp this session
// silently dropped files, and this test sat directly over it without
// noticing. mtools reads the image back here, so the contents are checked
// by something other than the code that wrote them.

namespace {

bool haveMtools()
{
    return !QStandardPaths::findExecutable(QStringLiteral("mcopy")).isEmpty();
}

QByteArray readFromImg(const QString &image, const QString &path)
{
    QProcess p;
    p.start(QStringLiteral("mcopy"),
            {QStringLiteral("-i"), image, QStringLiteral("::") + path,
             QStringLiteral("-")});
    if (!p.waitForFinished(rpi_test::kFixtureProcessTimeoutMs))
        return {};
    return p.readAllStandardOutput();
}

} // namespace

TEST_CASE("The files put in a boot.img are in the boot.img",
          "[secureboot][bootimg-contents]")
{
    if (!haveMtools())
        SKIP("mtools is needed to read the image back");

    ScratchDir scratch;
    const QString out = scratch.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    files.insert(QStringLiteral("cmdline.txt"), "console=serial0,115200\n");
    files.insert(QStringLiteral("start4.elf"), QByteArray(4096, '\x11'));

    REQUIRE(SecureBoot::createBootImg(files, out));

    CHECK(readFromImg(out, QStringLiteral("config.txt")) == QByteArray("arm_64bit=1\n"));
    CHECK(readFromImg(out, QStringLiteral("cmdline.txt"))
          == QByteArray("console=serial0,115200\n"));
    CHECK(readFromImg(out, QStringLiteral("start4.elf")) == QByteArray(4096, '\x11'));
}

TEST_CASE("A nested firmware tree survives into the boot.img",
          "[secureboot][bootimg-contents]")
{
    // The shape that broke: directories created out of order left the files
    // inside them missing, and the image came back reported as good.
    if (!haveMtools())
        SKIP("mtools is needed to read the image back");

    ScratchDir scratch;
    const QString out = scratch.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    files.insert(QStringLiteral("overlays/vc4-kms-v3d.dtbo"), "OVERLAY");
    files.insert(QStringLiteral("a/b/c/deep.bin"), "DEEP");

    REQUIRE(SecureBoot::createBootImg(files, out));

    CHECK(readFromImg(out, QStringLiteral("config.txt")) == QByteArray("arm_64bit=1\n"));
    CHECK(readFromImg(out, QStringLiteral("overlays/vc4-kms-v3d.dtbo"))
          == QByteArray("OVERLAY"));
    CHECK(readFromImg(out, QStringLiteral("a/b/c/deep.bin")) == QByteArray("DEEP"));
}

TEST_CASE("A boot.img is at least the FAT32 minimum", "[secureboot][bootimg-contents]")
{
    // Three small files come to a few kilobytes, but FAT32 needs 33 MB
    // before mkfs.vfat will make one at all. Sizing from the contents alone
    // would produce an image no tool can format.
    if (!haveMtools())
        SKIP("mtools is needed to read the image back");

    ScratchDir scratch;
    const QString out = scratch.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "x\n");

    REQUIRE(SecureBoot::createBootImg(files, out));
    CHECK(QFileInfo(out).size() >= 33 * 1024 * 1024);
}

TEST_CASE("A boot.img grows to hold what is put in it",
          "[secureboot][bootimg-contents]")
{
    // Past the minimum the size follows the contents, with room for the
    // filesystem's own structures on top.
    if (!haveMtools())
        SKIP("mtools is needed to read the image back");

    ScratchDir scratch;
    const QString out = scratch.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("big.bin"), QByteArray(48 * 1024 * 1024, '\x7e'));

    REQUIRE(SecureBoot::createBootImg(files, out));
    const qint64 size = QFileInfo(out).size();
    INFO("image size: " << size);
    CHECK(size > 48 * 1024 * 1024);
    CHECK(readFromImg(out, QStringLiteral("big.bin")).size() == 48 * 1024 * 1024);
}

// ══════════════════════════════════════════════════════════════
// Reading the RSA public key the board will be locked to
//
// parseSubjectPublicKeyInfoDerToNE turns the DER a key file yields into the
// 264 bytes the boot ROM expects: the modulus, little-endian, then the
// exponent, little-endian. It is what decides which key a device with
// programmed OTP will accept for the rest of its life, and OTP cannot be
// rewritten -- a board locked to the wrong bytes is not recoverable.
//
// Every refusal in it was uncovered. It is a pure function over a byte
// array, so the DER can be built by hand and each malformed shape given its
// own case: a parser that accepts something it should not and returns 264
// plausible-looking bytes is exactly the failure that cannot be undone.

namespace {

// Minimal ASN.1 writing, enough to build a SubjectPublicKeyInfo by hand.
QByteArray derLength(int n)
{
    QByteArray out;
    if (n < 0x80) {
        out.append(static_cast<char>(n));
    } else if (n < 0x100) {
        out.append(char(0x81));
        out.append(static_cast<char>(n));
    } else {
        out.append(char(0x82));
        out.append(static_cast<char>((n >> 8) & 0xFF));
        out.append(static_cast<char>(n & 0xFF));
    }
    return out;
}

QByteArray derTagged(quint8 tag, const QByteArray &contents)
{
    QByteArray out;
    out.append(static_cast<char>(tag));
    out.append(derLength(contents.size()));
    out.append(contents);
    return out;
}

// An INTEGER, with the leading zero ASN.1 prepends when the top bit is set
// so the value stays positive.
QByteArray derInteger(const QByteArray &beValue)
{
    QByteArray body = beValue;
    if (!body.isEmpty() && (static_cast<quint8>(body.at(0)) & 0x80))
        body.prepend(char(0x00));
    return derTagged(0x02, body);
}

QByteArray modulusOf(int bytes, quint8 firstByte = 0xC7)
{
    QByteArray n(bytes, char(0x5A));
    n[0] = static_cast<char>(firstByte);
    n[bytes - 1] = char(0x01);
    return n;
}

// SubjectPublicKeyInfo := SEQUENCE { AlgorithmIdentifier, BIT STRING }
QByteArray publicKeyInfo(const QByteArray &modulusBE,
                         const QByteArray &exponentBE)
{
    const QByteArray rsaPublicKey =
        derTagged(0x30, derInteger(modulusBE) + derInteger(exponentBE));

    QByteArray bitStringBody;
    bitStringBody.append(char(0x00));  // unused bits
    bitStringBody.append(rsaPublicKey);

    // A stand-in AlgorithmIdentifier; the parser skips over it by length.
    const QByteArray algorithm = derTagged(0x30, QByteArray(13, char(0x2A)));

    return derTagged(0x30, algorithm + derTagged(0x03, bitStringBody));
}

const QByteArray kExponent = QByteArray::fromHex("010001");  // 65537

} // namespace

TEST_CASE("A 2048-bit public key becomes the bytes the boot ROM wants",
          "[secureboot][pubkey]")
{
    const QByteArray modulus = modulusOf(256);
    const QByteArray parsed =
        SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(
            publicKeyInfo(modulus, kExponent));

    REQUIRE(parsed.size() == 264);

    // The modulus, reversed: the ROM reads it little-endian and DER holds it
    // big-endian. Getting this backwards would lock a board to a key nobody
    // holds.
    QByteArray expectedN;
    for (int i = modulus.size() - 1; i >= 0; --i)
        expectedN.append(modulus.at(i));
    CHECK(parsed.left(256) == expectedN);

    // 65537 little-endian in eight bytes.
    CHECK(parsed.mid(256) == QByteArray::fromHex("0100010000000000"));
}

TEST_CASE("The leading zero ASN.1 adds to a modulus is not part of it",
          "[secureboot][pubkey]")
{
    // A modulus with its top bit set is written with a 0x00 in front so the
    // INTEGER stays positive. Keeping it would shift every byte and yield a
    // key one byte too long.
    const QByteArray modulus = modulusOf(256, 0xFF);
    const QByteArray parsed =
        SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(
            publicKeyInfo(modulus, kExponent));

    REQUIRE(parsed.size() == 264);
    CHECK(static_cast<quint8>(parsed.at(255)) == 0xFF);
}

TEST_CASE("A key that is not 2048 bits is refused", "[secureboot][pubkey]")
{
    // The ROM format has room for exactly 256 bytes of modulus. A shorter
    // key padded out, or a longer one truncated, is a key the device would
    // not accept afterwards.
    CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(
              publicKeyInfo(modulusOf(128), kExponent)).isEmpty());
    CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(
              publicKeyInfo(modulusOf(384), kExponent)).isEmpty());
    CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(
              publicKeyInfo(modulusOf(512), kExponent)).isEmpty());
}

TEST_CASE("Nothing at all is refused", "[secureboot][pubkey]")
{
    // Defended twice: the explicit empty check, and the tag check after it,
    // which finds nothing to read either. Removing the first fails nothing,
    // so no claim is made for it -- what is pinned is the outcome.
    CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE({}).isEmpty());
}

TEST_CASE("A blob that is not a public key is refused", "[secureboot][pubkey]")
{
    CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(
              QByteArray("not der at all")).isEmpty());
    CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(
              QByteArray::fromHex("0201FF")).isEmpty());  // an INTEGER, not a SEQUENCE
}

TEST_CASE("A public key cut short at any point is refused",
          "[secureboot][pubkey]")
{
    // Every prefix of a valid key. A parser that reads past the end of a
    // truncated one is reading whatever follows it in memory and calling the
    // result a key.
    const QByteArray whole = publicKeyInfo(modulusOf(256), kExponent);
    REQUIRE(!SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(whole).isEmpty());

    QStringList accepted;
    for (int cut = 1; cut < whole.size(); ++cut) {
        if (!SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(whole.left(cut)).isEmpty())
            accepted << QString::number(cut);
    }
    CHECK(accepted.join(QStringLiteral(", ")).toStdString() == std::string());
}

TEST_CASE("A public key with the wrong shape inside is refused",
          "[secureboot][pubkey]")
{
    const QByteArray modulus = modulusOf(256);

    SECTION("the outer wrapper is not a SEQUENCE") {
        QByteArray der = publicKeyInfo(modulus, kExponent);
        der[0] = char(0x31);
        CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(der).isEmpty());
    }

    SECTION("the algorithm is not a SEQUENCE") {
        QByteArray der = publicKeyInfo(modulus, kExponent);
        // Just past the outer tag and its two-byte length.
        der[4] = char(0x05);
        CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(der).isEmpty());
    }

    // Also defended twice: replacing the BIT STRING tag check with a bare
    // bounds check still refuses this, because the unused-bits byte that
    // follows is checked as well and an OCTET STRING does not have one. The
    // case is kept for the shape it describes, not for a failure it was
    // watched to produce on its own.
    SECTION("the key is not wrapped in a BIT STRING") {
        const QByteArray rsaPublicKey =
            derTagged(0x30, derInteger(modulus) + derInteger(kExponent));
        const QByteArray algorithm = derTagged(0x30, QByteArray(13, char(0x2A)));
        // An OCTET STRING where the BIT STRING belongs.
        const QByteArray der =
            derTagged(0x30, algorithm + derTagged(0x04, rsaPublicKey));
        CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(der).isEmpty());
    }

    SECTION("the exponent is longer than the format holds") {
        // Eight bytes is the room there is for it.
        const QByteArray tooBig = QByteArray::fromHex("0102030405060708090A");
        CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(
                  publicKeyInfo(modulus, tooBig)).isEmpty());
    }

    SECTION("the modulus is not an INTEGER") {
        const QByteArray notAnInteger = derTagged(0x04, modulus);
        const QByteArray rsaPublicKey =
            derTagged(0x30, notAnInteger + derInteger(kExponent));
        QByteArray bitStringBody;
        bitStringBody.append(char(0x00));
        bitStringBody.append(rsaPublicKey);
        const QByteArray algorithm = derTagged(0x30, QByteArray(13, char(0x2A)));
        const QByteArray der =
            derTagged(0x30, algorithm + derTagged(0x03, bitStringBody));
        CHECK(SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(der).isEmpty());
    }
}
