/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The RSA signing behind secure boot, checked against openssl.
 *
 * Every existing case that exercises this goes through rpi-eeprom-digest, a
 * Linux tool, so on Windows they all skip -- which left the CryptoAPI
 * implementation at half its branches and nothing at all asserting that it
 * produces a signature a bootloader would accept. The one case that reached it
 * end to end, "A signed EEPROM is re-signed after its boot order changes",
 * stopped without saying why, because the reason is a qDebug and Catch2 does
 * not replay those.
 *
 * So this asks the question directly, and verifies the answer with openssl
 * rather than against a recorded blob: a signature is only correct if the
 * public half can check it.
 */

#include <catch2/catch_test_macros.hpp>

#include "secureboot_crypto.h"
#include "platform_tools.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

namespace {

// A key of the shape the product is given: whatever openssl makes by default,
// which is PKCS#8 on 3.x and PKCS#1 on 1.x. Both are meant to work.
struct RsaKey
{
    QTemporaryDir dir;
    QString path;

    RsaKey()
    {
        REQUIRE(dir.isValid());
        path = QDir(dir.path()).filePath(QStringLiteral("customer.pem"));
        QProcess openssl;
        openssl.start(rpi_test::toolPath(QStringLiteral("openssl")),
                      {QStringLiteral("genrsa"), QStringLiteral("-out"), path,
                       QStringLiteral("2048")});
        REQUIRE(openssl.waitForFinished(120000));
        REQUIRE(openssl.exitCode() == 0);
        REQUIRE(QFile::exists(path));
    }

    // What the file actually says it is, so a failure names the format rather
    // than leaving it to be guessed.
    QString header() const
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        return QString::fromLatin1(f.readLine()).trimmed();
    }
};

#define REQUIRE_OPENSSL()                                                      \
    if (!rpi_test::haveTool(QStringLiteral("openssl")))                        \
    SKIP("openssl is not installed, so no key can be made to sign with")

} // namespace

TEST_CASE("A digest is signed with the configured key", "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    RsaKey key;
    INFO("key format: " << key.header().toStdString());

    const QByteArray digest =
        QCryptographicHash::hash(QByteArrayLiteral("bootconf contents"),
                                 QCryptographicHash::Sha256);
    REQUIRE(digest.size() == 32);

    const QByteArray sig = SecureBootCrypto::rsaSignSha256(digest, key.path);
    INFO("signature length: " << sig.size());
    REQUIRE_FALSE(sig.isEmpty());
    // Hex of a 2048-bit signature: 256 bytes, so 512 characters.
    CHECK(sig.size() == 512);
    // And hex rather than raw, which is what the caller writes into the file.
    CHECK(QByteArray::fromHex(sig).size() == 256);
}

TEST_CASE("The signature verifies against the public half",
          "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    RsaKey key;
    INFO("key format: " << key.header().toStdString());

    const QByteArray message = QByteArrayLiteral("BOOT_ORDER=0xf416\n");
    const QByteArray digest =
        QCryptographicHash::hash(message, QCryptographicHash::Sha256);
    const QByteArray sigHex = SecureBootCrypto::rsaSignSha256(digest, key.path);
    REQUIRE_FALSE(sigHex.isEmpty());

    // Written out and checked by openssl, because a signature that is merely
    // the right length is not a signature. pkeyutl verifies the raw digest
    // with the same padding the bootloader expects.
    QTemporaryDir work;
    REQUIRE(work.isValid());
    const QString digestPath = QDir(work.path()).filePath(QStringLiteral("digest.bin"));
    const QString sigPath = QDir(work.path()).filePath(QStringLiteral("sig.bin"));
    {
        QFile d(digestPath);
        REQUIRE(d.open(QIODevice::WriteOnly));
        REQUIRE(d.write(digest) == digest.size());
    }
    {
        QFile s(sigPath);
        REQUIRE(s.open(QIODevice::WriteOnly));
        const QByteArray raw = QByteArray::fromHex(sigHex);
        REQUIRE(s.write(raw) == raw.size());
    }

    QProcess verify;
    verify.start(rpi_test::toolPath(QStringLiteral("openssl")),
                 {QStringLiteral("pkeyutl"), QStringLiteral("-verify"),
                  QStringLiteral("-inkey"), key.path,
                  QStringLiteral("-sigfile"), sigPath,
                  QStringLiteral("-in"), digestPath,
                  QStringLiteral("-pkeyopt"), QStringLiteral("digest:sha256")});
    REQUIRE(verify.waitForFinished(120000));
    const QString said = QString::fromUtf8(verify.readAllStandardOutput())
                         + QString::fromUtf8(verify.readAllStandardError());
    INFO("openssl said: " << said.toStdString());
    CHECK(verify.exitCode() == 0);
}

TEST_CASE("A key that is not a key is refused rather than signed with",
          "[secureboot][crypto]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("not-a-key.pem"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("-----BEGIN NOTHING-----\nnope\n-----END NOTHING-----\n");
    }

    const QByteArray digest =
        QCryptographicHash::hash(QByteArrayLiteral("x"), QCryptographicHash::Sha256);
    CHECK(SecureBootCrypto::rsaSignSha256(digest, path).isEmpty());
}

TEST_CASE("A key that is not there is refused", "[secureboot][crypto]")
{
    const QByteArray digest =
        QCryptographicHash::hash(QByteArrayLiteral("x"), QCryptographicHash::Sha256);
    CHECK(SecureBootCrypto::rsaSignSha256(
              digest, QStringLiteral("C:/nowhere/rpi-imager/absent.pem"))
              .isEmpty());
}

TEST_CASE("A digest of the wrong length is refused", "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    RsaKey key;
    // The contract is a raw 32-byte SHA-256. Anything else signed anyway would
    // produce a signature the bootloader cannot check.
    CHECK(SecureBootCrypto::rsaSignSha256(QByteArrayLiteral("short"), key.path).isEmpty());
}

TEST_CASE("The public key comes back in the boot ROM's own layout",
          "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    RsaKey key;
    INFO("key format: " << key.header().toStdString());

    const QByteArray blob = SecureBootCrypto::extractRsaPubkeyBin(key.path);
    REQUIRE_FALSE(blob.isEmpty());
    // 256 bytes of modulus followed by 8 of exponent.
    CHECK(blob.size() == 264);

    // 65537 little-endian in the exponent field, which is what openssl
    // generates and what the ROM expects to find.
    const QByteArray exponent = blob.right(8);
    CHECK(static_cast<unsigned char>(exponent.at(0)) == 0x01);
    CHECK(static_cast<unsigned char>(exponent.at(1)) == 0x00);
    CHECK(static_cast<unsigned char>(exponent.at(2)) == 0x01);
}

TEST_CASE("Two keys do not produce the same public blob",
          "[secureboot][crypto]")
{
    REQUIRE_OPENSSL();
    RsaKey a;
    RsaKey b;
    const QByteArray blobA = SecureBootCrypto::extractRsaPubkeyBin(a.path);
    const QByteArray blobB = SecureBootCrypto::extractRsaPubkeyBin(b.path);
    REQUIRE_FALSE(blobA.isEmpty());
    REQUIRE_FALSE(blobB.isEmpty());
    CHECK(blobA != blobB);
}

// ============================================================================
// When the provider refuses
// ============================================================================
// Signing goes through a dozen CryptoAPI and CNG calls, each with its own
// failure path, and none of those paths had ever run: a provider refusing is
// not something a test can arrange. They matter more here than most -- this
// signs the firmware a board will boot, and a half-built answer handed back
// as a signature is worse than no answer at all.
//
// One call is made to fail at a time, chosen by position, and every one of
// them has to give an empty answer rather than a short signature, a crash, or
// an exception out of a routine whose callers only check for empty.

#ifdef SECUREBOOT_CRYPTO_ENABLE_TEST_API

namespace SecureBootCryptoTesting {
void failAtCall(int ordinal);
int callsMade();
}

namespace {

// Disarms itself, so a case that fails part-way does not leave the injection
// armed for whatever runs next in this binary.
struct ArmedFailure
{
    explicit ArmedFailure(int ordinal) { SecureBootCryptoTesting::failAtCall(ordinal); }
    ~ArmedFailure() { SecureBootCryptoTesting::failAtCall(-1); }
};

} // namespace

TEST_CASE("Signing refuses rather than returning a part-made signature",
          "[secureboot][crypto][inject]")
{
    REQUIRE_OPENSSL();
    RsaKey key;

    const QByteArray digest =
        QCryptographicHash::hash(QByteArrayLiteral("bootconf contents"),
                                 QCryptographicHash::Sha256);
    REQUIRE(digest.size() == 32);

    // How many guarded calls a clean run makes, so every one is covered
    // without a count here that goes stale when one is added.
    int calls = 0;
    {
        ArmedFailure none(-1);
        REQUIRE_FALSE(SecureBootCrypto::rsaSignSha256(digest, key.path).isEmpty());
        calls = SecureBootCryptoTesting::callsMade();
    }
    INFO("guarded calls in a clean signing run: " << calls);
    REQUIRE(calls > 0);

    for (int ordinal = 0; ordinal < calls; ++ordinal) {
        INFO("failing call " << ordinal << " of " << calls);
        ArmedFailure armed(ordinal);
        QByteArray sig;
        REQUIRE_NOTHROW(sig = SecureBootCrypto::rsaSignSha256(digest, key.path));
        CHECK(sig.isEmpty());
    }
}

TEST_CASE("Extracting the public key refuses rather than returning part of one",
          "[secureboot][crypto][inject]")
{
    // The same story on the other entry point. What this returns is spliced
    // into the boot image, so a short or truncated blob is a board that will
    // not verify its own firmware.
    REQUIRE_OPENSSL();
    RsaKey key;

    int calls = 0;
    {
        ArmedFailure none(-1);
        REQUIRE_FALSE(SecureBootCrypto::extractRsaPubkeyBin(key.path).isEmpty());
        calls = SecureBootCryptoTesting::callsMade();
    }
    INFO("guarded calls in a clean extraction: " << calls);
    REQUIRE(calls > 0);

    for (int ordinal = 0; ordinal < calls; ++ordinal) {
        INFO("failing call " << ordinal << " of " << calls);
        ArmedFailure armed(ordinal);
        QByteArray blob;
        REQUIRE_NOTHROW(blob = SecureBootCrypto::extractRsaPubkeyBin(key.path));
        CHECK(blob.isEmpty());
    }
}

TEST_CASE("With nothing armed the signature is still correct",
          "[secureboot][crypto][inject]")
{
    // The injection is compiled into this build, so it is worth showing it
    // does nothing when it is not armed -- otherwise the cases above could be
    // passing because signing never works here at all.
    REQUIRE_OPENSSL();
    RsaKey key;
    ArmedFailure none(-1);

    const QByteArray digest =
        QCryptographicHash::hash(QByteArrayLiteral("unarmed"),
                                 QCryptographicHash::Sha256);
    const QByteArray sig = SecureBootCrypto::rsaSignSha256(digest, key.path);
    CHECK(sig.size() == 512);
}

#endif // SECUREBOOT_CRYPTO_ENABLE_TEST_API

// ============================================================================
// Making a key pair
// ============================================================================
// Windows does not ship openssl, and the provisioner shelled out to it to
// make the secure boot key. On a machine with neither Git for Windows nor
// MSYS -- which is most of them -- that simply failed.
//
// The replacement has to produce a key openssl itself accepts, and a public
// key whose DER is byte-for-byte what openssl would write: the SHA-256 of
// that DER is fused into the device and cannot be changed afterwards.

TEST_CASE("A generated key pair is written as two PEM files", "[secureboot][keygen]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString priv = scratch.filePath(QStringLiteral("private.pem"));
    const QString pub = scratch.filePath(QStringLiteral("public.pem"));

    REQUIRE(SecureBootCrypto::generateRsaKeyPair(priv, pub));

    QFile pf(priv);
    REQUIRE(pf.open(QIODevice::ReadOnly));
    const QByteArray privPem = pf.readAll();
    QFile qf(pub);
    REQUIRE(qf.open(QIODevice::ReadOnly));
    const QByteArray pubPem = qf.readAll();

    CHECK(privPem.contains("PRIVATE KEY-----"));
    CHECK(pubPem.startsWith("-----BEGIN PUBLIC KEY-----"));
    CHECK(pubPem.trimmed().endsWith("-----END PUBLIC KEY-----"));
    // Armour and base64, nothing else.
    CHECK(privPem.size() > 1000);
}

TEST_CASE("A generated key signs, and the signature verifies", "[secureboot][keygen]")
{
    // The whole point of the key. Signing goes through the same CNG path the
    // product uses, so this proves the generated key is one it can drive.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString priv = scratch.filePath(QStringLiteral("private.pem"));
    const QString pub = scratch.filePath(QStringLiteral("public.pem"));
    REQUIRE(SecureBootCrypto::generateRsaKeyPair(priv, pub));

    const QByteArray digest =
        QCryptographicHash::hash(QByteArrayLiteral("boot.img contents"),
                                 QCryptographicHash::Sha256);
    const QByteArray sig = SecureBootCrypto::rsaSignSha256(digest, priv);
    INFO("signature length: " << sig.size());
    REQUIRE_FALSE(sig.isEmpty());
    CHECK(sig.size() == 512);   // 256 bytes of RSA-2048, hex
}

TEST_CASE("The generated public key is the one the private key implies",
          "[secureboot][keygen]")
{
    // Derived from the private half rather than written separately, so a pair
    // that does not match would sign with one key and fuse the hash of
    // another -- a board that will not boot its own firmware, permanently.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString priv = scratch.filePath(QStringLiteral("private.pem"));
    const QString pub = scratch.filePath(QStringLiteral("public.pem"));
    REQUIRE(SecureBootCrypto::generateRsaKeyPair(priv, pub));

    // extractRsaPubkeyBin takes the private key -- every platform derives the
    // public half from it -- so the public PEM is checked by decoding it and
    // parsing out the same modulus and exponent.
    const QByteArray fromPrivate = SecureBootCrypto::extractRsaPubkeyBin(priv);
    REQUIRE(fromPrivate.size() == 264);

    const QByteArray der = SecureBootCrypto::publicKeyPemToDer(pub);
    REQUIRE_FALSE(der.isEmpty());
    const QByteArray fromPublic =
        SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(der);
    REQUIRE(fromPublic.size() == 264);

    CHECK(fromPrivate == fromPublic);
}

TEST_CASE("Two generated key pairs are different", "[secureboot][keygen]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString privA = scratch.filePath(QStringLiteral("a.pem"));
    const QString pubA = scratch.filePath(QStringLiteral("a.pub.pem"));
    const QString privB = scratch.filePath(QStringLiteral("b.pem"));
    const QString pubB = scratch.filePath(QStringLiteral("b.pub.pem"));
    REQUIRE(SecureBootCrypto::generateRsaKeyPair(privA, pubA));
    REQUIRE(SecureBootCrypto::generateRsaKeyPair(privB, pubB));

    CHECK(SecureBootCrypto::extractRsaPubkeyBin(privA) !=
          SecureBootCrypto::extractRsaPubkeyBin(privB));
}

TEST_CASE("openssl accepts the generated key and agrees about its public half",
          "[secureboot][keygen]")
{
    // The check that matters most. The SHA-256 of the public key's DER is
    // fused into the device, so our encoding has to be byte-for-byte what
    // openssl writes -- down to the explicit ASN.1 NULL in the algorithm
    // identifier. A different encoding is a different hash and a board fused
    // to a key nobody can prove they hold.
    REQUIRE_OPENSSL();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString priv = scratch.filePath(QStringLiteral("private.pem"));
    const QString pub = scratch.filePath(QStringLiteral("public.pem"));
    REQUIRE(SecureBootCrypto::generateRsaKeyPair(priv, pub));

    // openssl reads the private key and derives the public half itself.
    const QString derived = scratch.filePath(QStringLiteral("derived.pem"));
    QProcess pubout;
    pubout.start(rpi_test::toolPath(QStringLiteral("openssl")),
                 {QStringLiteral("rsa"), QStringLiteral("-in"), priv,
                  QStringLiteral("-pubout"), QStringLiteral("-out"), derived});
    REQUIRE(pubout.waitForFinished(30000));
    INFO("openssl rsa -pubout: " << pubout.readAllStandardError().constData());
    REQUIRE(pubout.exitCode() == 0);

    QFile ours(pub);
    REQUIRE(ours.open(QIODevice::ReadOnly));
    QFile theirs(derived);
    REQUIRE(theirs.open(QIODevice::ReadOnly));

    // Compare the base64 bodies, so a line ending cannot fail this.
    auto body = [](QByteArray pem) {
        pem.replace("\r", "").replace("\n", "");
        return pem;
    };
    CHECK(body(ours.readAll()) == body(theirs.readAll()));
}

TEST_CASE("The DER we take the OTP hash over is the DER openssl prints",
          "[secureboot][keygen]")
{
    // The OTP hash is SHA-256 of this DER and it is fused into the device
    // permanently. It used to come from `openssl rsa -pubin -outform DER`;
    // it now comes from decoding the PEM body here. Those must be the same
    // bytes, or a board provisioned by one build will not accept firmware
    // signed for the other.
    REQUIRE_OPENSSL();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString priv = scratch.filePath(QStringLiteral("private.pem"));
    const QString pub = scratch.filePath(QStringLiteral("public.pem"));
    REQUIRE(SecureBootCrypto::generateRsaKeyPair(priv, pub));

    const QString derPath = scratch.filePath(QStringLiteral("public.der"));
    QProcess openssl;
    openssl.start(rpi_test::toolPath(QStringLiteral("openssl")),
                  {QStringLiteral("rsa"), QStringLiteral("-pubin"),
                   QStringLiteral("-in"), pub, QStringLiteral("-outform"),
                   QStringLiteral("DER"), QStringLiteral("-out"), derPath});
    REQUIRE(openssl.waitForFinished(30000));
    INFO("openssl: " << openssl.readAllStandardError().constData());
    REQUIRE(openssl.exitCode() == 0);

    QFile f(derPath);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray theirs = f.readAll();
    const QByteArray ours = SecureBootCrypto::publicKeyPemToDer(pub);

    REQUIRE_FALSE(ours.isEmpty());
    INFO("ours " << ours.size() << " bytes, openssl " << theirs.size());
    CHECK(ours == theirs);
}

TEST_CASE("A PEM that is not a public key yields no DER", "[secureboot][keygen]")
{
    // Hashing whatever came back would give a well-formed 32-byte value that
    // is the hash of nothing in particular -- and that value gets fused.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    const QString notPem = scratch.filePath(QStringLiteral("notes.txt"));
    { QFile f(notPem); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("hello\n"); }
    CHECK(SecureBootCrypto::publicKeyPemToDer(notPem).isEmpty());

    // A private key is not a public one, however well-formed.
    const QString priv = scratch.filePath(QStringLiteral("private.pem"));
    const QString pub = scratch.filePath(QStringLiteral("public.pem"));
    REQUIRE(SecureBootCrypto::generateRsaKeyPair(priv, pub));
    CHECK(SecureBootCrypto::publicKeyPemToDer(priv).isEmpty());

    // Armour with rubbish inside it.
    const QString broken = scratch.filePath(QStringLiteral("broken.pem"));
    {
        QFile f(broken);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("-----BEGIN PUBLIC KEY-----\nnot base64 at all !!!\n-----END PUBLIC KEY-----\n");
    }
    CHECK(SecureBootCrypto::publicKeyPemToDer(broken).isEmpty());

    CHECK(SecureBootCrypto::publicKeyPemToDer(
              scratch.filePath(QStringLiteral("absent.pem"))).isEmpty());
}

#ifdef SECUREBOOT_CRYPTO_ENABLE_TEST_API

TEST_CASE("Generating a key refuses rather than leaving half a pair",
          "[secureboot][keygen][inject]")
{
    // Generation walks a dozen CNG and CryptoAPI calls, each with its own
    // failure path. A refusal part-way must leave nothing usable behind: a
    // private key with no public half is still a real RSA key, and Imager's
    // own file chooser would offer it to sign with.
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    int calls = 0;
    {
        ArmedFailure none(-1);
        const QString priv = scratch.filePath(QStringLiteral("clean.pem"));
        const QString pub = scratch.filePath(QStringLiteral("clean.pub.pem"));
        REQUIRE(SecureBootCrypto::generateRsaKeyPair(priv, pub));
        calls = SecureBootCryptoTesting::callsMade();
    }
    INFO("guarded calls in a clean generation: " << calls);
    REQUIRE(calls > 0);

    for (int ordinal = 0; ordinal < calls; ++ordinal) {
        INFO("failing call " << ordinal << " of " << calls);
        const QString priv =
            scratch.filePath(QStringLiteral("priv-%1.pem").arg(ordinal));
        const QString pub =
            scratch.filePath(QStringLiteral("pub-%1.pem").arg(ordinal));

        ArmedFailure armed(ordinal);
        bool made = true;
        REQUIRE_NOTHROW(made = SecureBootCrypto::generateRsaKeyPair(priv, pub));
        CHECK_FALSE(made);

        // Nothing usable left where the caller would look for it.
        CHECK_FALSE(QFile::exists(pub));
        if (QFile::exists(priv)) {
            INFO("a private key was left at " << priv.toStdString());
            CHECK_FALSE(QFile::exists(priv));
        }
    }
}

#endif // SECUREBOOT_CRYPTO_ENABLE_TEST_API
