/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Unit tests for the in-process bootconf.sig builder.
 *
 * The unsigned path is fully deterministic and tests run anywhere.
 * The signed path needs a 2048-bit RSA key; the test generates a
 * throwaway one with the platform's native crypto when possible
 * (skipped if not). The cross-check against `rpi-eeprom-digest`
 * is gated on that tool being on PATH.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "fastboot/eeprom_signer.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstring>
#include <span>
#include <string>
#include <vector>

// Stub: the linked platform secureboot_crypto_*.cpp on Linux/macOS
// also defines extractRsaPubkeyBin, which calls parseSubjectPublicKeyInfoDerToNE.
// That real implementation lives in secureboot.cpp, which has a large
// transitive dependency graph (devicewrapperfatpartition, etc.) we don't
// want to drag into a focused unit-test binary. Our tests only exercise
// rsaSignSha256, so an empty stub satisfies the linker.
#if !defined(_WIN32)
namespace SecureBootCrypto {
QByteArray parseSubjectPublicKeyInfoDerToNE(const QByteArray&) { return {}; }
}
#endif

using namespace fastboot;

namespace {

std::vector<uint8_t> asBytes(const std::string& s)
{
    return {s.begin(), s.end()};
}

std::string asString(std::span<const uint8_t> bytes)
{
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Helper: generate a throwaway RSA-2048 PEM key via `openssl genpkey`.
// Returns the path if openssl is available, or empty if not (caller
// skips the test). Used only by the signed-path tests --- the unsigned
// path doesn't need a key.
QString generateThrowawayPemKey(QTemporaryDir& dir)
{
    QString tool = QStandardPaths::findExecutable("openssl");
    if (tool.isEmpty()) return {};

    QString path = dir.filePath("test.pem");
    QProcess p;
    p.start(tool, QStringList()
        << "genpkey" << "-algorithm" << "RSA"
        << "-pkeyopt" << "rsa_keygen_bits:2048"
        << "-out"     << path);
    if (!p.waitForStarted(5000)) return {};
    if (!p.waitForFinished(15000) || p.exitCode() != 0) return {};
    return path;
}

} // namespace

// ── Unsigned digest (always runs) ─────────────────────────────────────

TEST_CASE("buildEepromSig (unsigned) emits sha256 + ts lines, no rsa2048",
          "[eeprom][sig]")
{
    auto input = asBytes("[all]\nBOOT_ORDER=0xf41\n");

    EepromSignerConfig cfg;
    cfg.sourceDateEpoch = 1700000000; // deterministic

    auto r = buildEepromSig(std::span<const uint8_t>(input), cfg);
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());

    std::string out = asString(r.sigBytes);

    // Compute expected SHA-256 line.
    QByteArray expectedHex = QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char*>(input.data()),
                       static_cast<qsizetype>(input.size())),
        QCryptographicHash::Sha256).toHex();

    const std::string expected = std::string(expectedHex.constData(), expectedHex.size())
                               + "\nts: 1700000000\n";
    CHECK(out == expected);
    CHECK(out.find("rsa2048:") == std::string::npos);
}

TEST_CASE("buildEepromSig (unsigned) uses current time when sourceDateEpoch unset",
          "[eeprom][sig]")
{
    auto input = asBytes("hello");
    EepromSignerConfig cfg; // no timestamp
    auto r = buildEepromSig(std::span<const uint8_t>(input), cfg);
    REQUIRE(r.ok);

    std::string out = asString(r.sigBytes);
    INFO("out=" << out);
    // SHA-256 (64 hex) + "\nts: <digits>\n"
    REQUIRE(out.size() > 64);
    CHECK(out.substr(0, 64).find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK_THAT(out, Catch::Matchers::ContainsSubstring("\nts: "));
    CHECK(out.find("rsa2048:") == std::string::npos);
}

// ── Signed path: requires openssl to generate a throwaway key ─────────

TEST_CASE("buildEepromSig (signed) emits an rsa2048 line of correct length",
          "[eeprom][sig][signed]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    QString keyPath = generateThrowawayPemKey(scratch);
    if (keyPath.isEmpty()) {
        SKIP("openssl not available to generate a throwaway RSA key");
    }

    auto input = asBytes("[all]\nBOOT_ORDER=0xf41\n");

    EepromSignerConfig cfg;
    cfg.pemKeyPath = keyPath.toStdString();
    cfg.sourceDateEpoch = 1700000000;

    auto r = buildEepromSig(std::span<const uint8_t>(input), cfg);
    if (!r.ok) {
        // SecureBootCrypto can fail on systems where the platform
        // signer isn't usable (e.g. CI containers without CNG/Security).
        SKIP("platform RSA signing unavailable: " << r.error);
    }
    REQUIRE(r.ok);

    std::string out = asString(r.sigBytes);
    INFO("sig=\n" << out);

    // Structure: 64 hex chars + \n + "ts: <epoch>\n" + "rsa2048: <512 hex>\n"
    CHECK(out.find("ts: 1700000000\n") != std::string::npos);

    auto rsaPos = out.find("rsa2048: ");
    REQUIRE(rsaPos != std::string::npos);
    auto eol = out.find('\n', rsaPos);
    REQUIRE(eol != std::string::npos);
    const std::string hex = out.substr(rsaPos + std::strlen("rsa2048: "),
                                        eol - (rsaPos + std::strlen("rsa2048: ")));
    CHECK(hex.size() == 512); // 2048-bit RSA -> 256 bytes -> 512 hex chars
    CHECK(hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos);
}

// ── Cross-check vs rpi-eeprom-digest ─────────────────────────────────

TEST_CASE("buildEepromSig (unsigned) bytes match rpi-eeprom-digest output",
          "[eeprom][sig][crosscheck]")
{
    QString tool = QStandardPaths::findExecutable("rpi-eeprom-digest");
    if (tool.isEmpty()) {
        SKIP("rpi-eeprom-digest not on PATH");
    }

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString inputPath  = scratch.filePath("input.bin");
    const QString refOutPath = scratch.filePath("ref.sig");

    auto input = asBytes("[all]\nBOOT_ORDER=0xf41\nFOO=bar\n");
    {
        QFile f(inputPath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(reinterpret_cast<const char*>(input.data()),
                static_cast<qint64>(input.size()));
    }

    // Run reference with deterministic timestamp.
    QProcess p;
    auto env = p.processEnvironment();
    env.insert("SOURCE_DATE_EPOCH", "1700000000");
    p.setProcessEnvironment(env);
    p.start(tool, QStringList() << "-i" << inputPath << "-o" << refOutPath);
    REQUIRE(p.waitForStarted(5000));
    REQUIRE(p.waitForFinished(15000));
    REQUIRE(p.exitCode() == 0);

    QFile rf(refOutPath);
    REQUIRE(rf.open(QIODevice::ReadOnly));
    QByteArray ref = rf.readAll();

    EepromSignerConfig cfg;
    cfg.sourceDateEpoch = 1700000000;
    auto r = buildEepromSig(std::span<const uint8_t>(input), cfg);
    REQUIRE(r.ok);

    REQUIRE(r.sigBytes.size() == static_cast<size_t>(ref.size()));
    std::vector<uint8_t> refVec(reinterpret_cast<const uint8_t*>(ref.constData()),
                                 reinterpret_cast<const uint8_t*>(ref.constData()) + ref.size());
    CHECK(r.sigBytes == refVec);
}

TEST_CASE("buildEepromSig (signed) bytes match rpi-eeprom-digest -k output",
          "[eeprom][sig][crosscheck][signed]")
{
    QString digestTool = QStandardPaths::findExecutable("rpi-eeprom-digest");
    if (digestTool.isEmpty()) {
        SKIP("rpi-eeprom-digest not on PATH");
    }

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    QString keyPath = generateThrowawayPemKey(scratch);
    if (keyPath.isEmpty()) {
        SKIP("openssl not available to generate a throwaway RSA key");
    }

    auto input = asBytes("[all]\nBOOT_ORDER=0xf416\n");

    const QString inputPath  = scratch.filePath("input.bin");
    const QString refOutPath = scratch.filePath("ref.sig");
    {
        QFile f(inputPath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(reinterpret_cast<const char*>(input.data()),
                static_cast<qint64>(input.size()));
    }

    QProcess p;
    auto env = p.processEnvironment();
    env.insert("SOURCE_DATE_EPOCH", "1700000000");
    p.setProcessEnvironment(env);
    p.start(digestTool, QStringList()
        << "-i" << inputPath << "-o" << refOutPath << "-k" << keyPath);
    REQUIRE(p.waitForStarted(5000));
    REQUIRE(p.waitForFinished(15000));
    REQUIRE(p.exitCode() == 0);

    QFile rf(refOutPath);
    REQUIRE(rf.open(QIODevice::ReadOnly));
    QByteArray ref = rf.readAll();

    EepromSignerConfig cfg;
    cfg.pemKeyPath = keyPath.toStdString();
    cfg.sourceDateEpoch = 1700000000;
    auto r = buildEepromSig(std::span<const uint8_t>(input), cfg);
    if (!r.ok) {
        SKIP("platform RSA signing unavailable: " << r.error);
    }

    // RSA-PKCS#1 v1.5 over the same key + same message is deterministic,
    // so bytes must match exactly.
    REQUIRE(r.sigBytes.size() == static_cast<size_t>(ref.size()));
    std::vector<uint8_t> refVec(reinterpret_cast<const uint8_t*>(ref.constData()),
                                 reinterpret_cast<const uint8_t*>(ref.constData()) + ref.size());
    INFO("ours: " << asString(r.sigBytes));
    INFO("ref:  " << std::string(ref.constData(), ref.size()));
    CHECK(r.sigBytes == refVec);
}

// ---------------------------------------------------------------------------
// The three ways signing is refused.
//
// buildEepromSig() either returns a signature block or an error string, and
// the caller writes whatever comes back. Each of these refusals exists so a
// bad key produces a message rather than an EEPROM image the board will not
// accept -- and none of them had ever been taken.
// ---------------------------------------------------------------------------

TEST_CASE("A key path that is not there is refused before anything is signed",
          "[eeprom-signer]")
{
    // The commonest way to get this wrong: a key moved, renamed, or typed in
    // by hand. Signing has to stop here, because everything downstream treats
    // an empty signature as "unsigned" rather than "failed".
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    EepromSignerConfig config;
    config.pemKeyPath = dir.filePath(QStringLiteral("no-such-key.pem")).toStdString();
    config.sourceDateEpoch = 1700000000;

    const auto input = asBytes("pieeprom contents");
    const EepromSignResult r = buildEepromSig(input, config);

    CHECK_FALSE(r.error.empty());
    CHECK_THAT(r.error, Catch::Matchers::ContainsSubstring("not readable"));
    CHECK(r.sigBytes.empty());
}

TEST_CASE("A key file that is not a key is refused", "[eeprom-signer]")
{
    // The file is there and readable, so the check above passes; openssl is
    // what discovers it is not a key, and it reports that by handing back
    // nothing. Treating an empty signature as success would write an image
    // whose rsa2048 line is missing entirely.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString bogus = dir.filePath(QStringLiteral("not-a-key.pem"));
    {
        QFile f(bogus);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("-----BEGIN RSA PRIVATE KEY-----\nnot base64 at all\n"
                "-----END RSA PRIVATE KEY-----\n");
    }

    EepromSignerConfig config;
    config.pemKeyPath = bogus.toStdString();
    config.sourceDateEpoch = 1700000000;

    const EepromSignResult r = buildEepromSig(asBytes("pieeprom contents"), config);

    CHECK_FALSE(r.error.empty());
    CHECK_THAT(r.error, Catch::Matchers::ContainsSubstring("signing failed"));
    CHECK(r.sigBytes.empty());
}

TEST_CASE("A key of the wrong size is refused rather than signed with",
          "[eeprom-signer]")
{
    // A 1024-bit key signs perfectly well and produces 256 hex characters
    // where the bootloader expects 512. Nothing about that fails until the
    // board rejects the EEPROM, by which point the user has a device that
    // will not boot and no indication why -- so the length is checked here.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("openssl"));
    if (tool.isEmpty())
        SKIP("openssl is not available, so no short key can be made");

    const QString shortKey = dir.filePath(QStringLiteral("rsa1024.pem"));
    QProcess p;
    p.start(tool, QStringList() << "genpkey" << "-algorithm" << "RSA"
                                << "-pkeyopt" << "rsa_keygen_bits:1024"
                                << "-out" << shortKey);
    if (!p.waitForStarted(5000) || !p.waitForFinished(15000) || p.exitCode() != 0)
        SKIP("openssl would not generate a 1024-bit key");

    EepromSignerConfig config;
    config.pemKeyPath = shortKey.toStdString();
    config.sourceDateEpoch = 1700000000;

    const EepromSignResult r = buildEepromSig(asBytes("pieeprom contents"), config);

    INFO("error: " << r.error);
    CHECK_FALSE(r.error.empty());
    CHECK_THAT(r.error, Catch::Matchers::ContainsSubstring("signature length"));
    CHECK(r.sigBytes.empty());
}
