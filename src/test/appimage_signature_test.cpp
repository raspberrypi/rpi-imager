// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The AppImage signature the Linux helper authenticates its client by. Every
// signed AppImage was refused, and fixing only that would have accepted a
// copied signature on a modified file. Each case signs a real ELF with a
// throwaway key.

#include <catch2/catch_test_macros.hpp>

#include "linux/appimage_signature.h"

#include <gnutls/crypto.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using rpi_imager::appimage::findElfSection;
using rpi_imager::appimage::SectionInfo;
using rpi_imager::appimage::verifyEmbeddedSignature;
using rpi_imager::appimage::VerifyResult;

namespace {

constexpr std::size_t kSigLen = 1024;
constexpr std::size_t kKeyLen = 8192;

int sh(const std::string& cmd) {
    return std::system((cmd + " >/dev/null 2>&1").c_str());
}

std::string capture(const std::string& cmd) {
    std::string out;
    if (FILE* p = ::popen(cmd.c_str(), "r")) {
        char buf[256];
        while (std::fgets(buf, sizeof(buf), p)) {
            out += buf;
        }
        ::pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
}

std::vector<char> slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void patch(const fs::path& p, std::size_t offset, std::vector<char> bytes, std::size_t pad_to) {
    bytes.resize(pad_to, '\0');
    std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(static_cast<std::streamoff>(offset));
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Two throwaway keys and an ELF carrying empty signature sections.
struct Fixture {
    fs::path dir;
    fs::path keys;
    fs::path app;
    std::string fpr;
    std::string other_fpr;
    SectionInfo sig {};
    SectionInfo key {};
    bool ok = false;

    Fixture() {
        char tmpl[] = "/tmp/appimage-sig-test-XXXXXX";
        if (!::mkdtemp(tmpl)) {
            return;
        }
        dir = tmpl;
        keys = dir / "keys";
        fs::create_directory(keys);
        fs::permissions(keys, fs::perms::owner_all, fs::perm_options::replace);
        app = dir / "app";
        fs::copy_file("/bin/true", app);

        const std::string gpg = "gpg --homedir '" + keys.string() + "' --batch --passphrase ''";
        if (sh(gpg + " --quick-gen-key 'Imager test <signer@example.invalid>' ed25519 sign never") != 0
            || sh(gpg + " --quick-gen-key 'Someone else <other@example.invalid>' ed25519 sign never") != 0) {
            return;
        }
        const std::string fpr_of = gpg + " --with-colons --fingerprint ";
        fpr = capture(fpr_of + "signer@example.invalid | awk -F: '/^fpr:/ {print toupper($10); exit}'");
        other_fpr = capture(fpr_of + "other@example.invalid | awk -F: '/^fpr:/ {print toupper($10); exit}'");

        std::ofstream((dir / "sig.zero").string(), std::ios::binary) << std::string(kSigLen, '\0');
        std::ofstream((dir / "key.zero").string(), std::ios::binary) << std::string(kKeyLen, '\0');
        ok = !fpr.empty() && !other_fpr.empty()
            && sh("objcopy --add-section .sha256_sig='" + (dir / "sig.zero").string()
                  + "' --add-section .sig_key='" + (dir / "key.zero").string() + "' '"
                  + app.string() + "'") == 0
            && findElfSection(app.string(), ".sha256_sig", sig)
            && findElfSection(app.string(), ".sig_key", key);
    }

    ~Fixture() {
        if (!keys.empty()) {
            sh("gpgconf --homedir '" + keys.string() + "' --kill all");
        }
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Embed `embedded_key`'s public key, then sign as `signer`, the way
    // appimagetool does: SHA-256 hex of the file with both sections zeroed.
    void sign(const std::string& signer, const std::string& embedded_key) {
        const std::string gpg = "gpg --homedir '" + keys.string() + "' --batch --yes --passphrase ''";
        sh(gpg + " --armor --output '" + (dir / "pub.asc").string() + "' --export " + embedded_key);
        patch(app, key.offset, slurp(dir / "pub.asc"), kKeyLen);
        patch(app, sig.offset, {}, kSigLen);

        std::vector<char> bytes = slurp(app);
        std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(sig.offset), kSigLen, '\0');
        if (key.offset == sig.offset + sig.length) {
            std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(key.offset), kKeyLen, '\0');
        }
        unsigned char digest[32];
        gnutls_hash_fast(GNUTLS_DIG_SHA256, bytes.data(), bytes.size(), digest);
        std::string hex;
        char two[3];
        for (unsigned char b : digest) {
            std::snprintf(two, sizeof(two), "%02x", b);
            hex += two;
        }
        std::ofstream((dir / "digest").string(), std::ios::binary) << hex;
        sh(gpg + " --local-user " + signer + " --armor --detach-sign --output '"
           + (dir / "digest.asc").string() + "' '" + (dir / "digest").string() + "'");
        patch(app, sig.offset, slurp(dir / "digest.asc"), kSigLen);
    }
};

bool toolsPresent() {
    return sh("command -v gpg && command -v gpgconf && command -v objcopy") == 0
        && fs::exists("/bin/true");
}

} // namespace

TEST_CASE("A signature from the pinned key is accepted", "[appimage][signature]")
{
    if (!toolsPresent()) SKIP("needs gpg and objcopy");
    Fixture f;
    REQUIRE(f.ok);
    f.sign("signer@example.invalid", "signer@example.invalid");

    CHECK(verifyEmbeddedSignature(f.app.string(), {f.fpr}) == VerifyResult::Ok);
    CHECK(verifyEmbeddedSignature(f.app.string(), {}) == VerifyResult::Ok);
}

TEST_CASE("A good signature from a key that is not pinned is refused", "[appimage][signature]")
{
    if (!toolsPresent()) SKIP("needs gpg and objcopy");
    Fixture f;
    REQUIRE(f.ok);
    f.sign("signer@example.invalid", "signer@example.invalid");

    CHECK(verifyEmbeddedSignature(f.app.string(), {f.other_fpr}) == VerifyResult::UntrustedKey);
}

TEST_CASE("A file changed after signing is refused although the signature names the pinned key",
          "[appimage][signature]")
{
    if (!toolsPresent()) SKIP("needs gpg and objcopy");
    Fixture f;
    REQUIRE(f.ok);
    f.sign("signer@example.invalid", "signer@example.invalid");
    std::vector<char> one = slurp(f.app);
    patch(f.app, 64, {static_cast<char>(one[64] ^ 0x01)}, 1);

    CHECK(verifyEmbeddedSignature(f.app.string(), {f.fpr}) == VerifyResult::Tampered);
}

TEST_CASE("A signature the embedded key did not make is refused", "[appimage][signature]")
{
    if (!toolsPresent()) SKIP("needs gpg and objcopy");
    Fixture f;
    REQUIRE(f.ok);
    f.sign("other@example.invalid", "signer@example.invalid");

    CHECK(verifyEmbeddedSignature(f.app.string(), {f.other_fpr}) == VerifyResult::Tampered);
    CHECK(verifyEmbeddedSignature(f.app.string(), {f.fpr}) == VerifyResult::Tampered);
}

TEST_CASE("An AppImage with empty signature sections reads as unsigned", "[appimage][signature]")
{
    if (!toolsPresent()) SKIP("needs gpg and objcopy");
    Fixture f;
    REQUIRE(f.ok);

    CHECK(verifyEmbeddedSignature(f.app.string(), {f.fpr}) == VerifyResult::Unsigned);
}
