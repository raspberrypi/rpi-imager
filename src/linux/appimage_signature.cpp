// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "appimage_signature.h"

#include <gnutls/crypto.h>
#include <gpgme.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace rpi_imager::appimage {

namespace {

constexpr char kSigSection[] = ".sha256_sig";
constexpr char kKeySection[] = ".sig_key";

struct ElfIdent {
    unsigned char ei_class = 0;
    bool little_endian = true;
};

bool readAt(int fd, off_t off, void* buf, std::size_t len) {
    if (lseek(fd, off, SEEK_SET) < 0) {
        return false;
    }
    std::size_t done = 0;
    auto* out = static_cast<unsigned char*>(buf);
    while (done < len) {
        const ssize_t n = ::read(fd, out + done, len - done);
        if (n <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool parseIdent(int fd, ElfIdent& ident) {
    unsigned char hdr[16] = {0};
    if (!readAt(fd, 0, hdr, sizeof(hdr))) {
        return false;
    }
    if (hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') {
        return false;
    }
    ident.ei_class = hdr[4];
    ident.little_endian = hdr[5] == 1;
    return ident.ei_class == 1 || ident.ei_class == 2;
}

template <typename T>
T readLe(const unsigned char* p) {
    T v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<T>(static_cast<T>(p[i]) << (8 * i));
    }
    return v;
}

std::string normalizeFingerprint(std::string fp) {
    fp.erase(std::remove_if(fp.begin(), fp.end(),
                            [](unsigned char c) { return std::isspace(c); }),
             fp.end());
    std::transform(fp.begin(), fp.end(), fp.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return fp;
}

bool sha256FileZeroedSections(int fd,
                            std::size_t skip_offset,
                            std::size_t skip_length,
                            unsigned char out_digest[32]) {
    gnutls_hash_hd_t hd = nullptr;
    if (gnutls_hash_init(&hd, GNUTLS_DIG_SHA256) != 0) {
        return false;
    }

    constexpr std::size_t kBuf = 1024 * 1024;
    std::vector<unsigned char> buf(kBuf);

    if (lseek(fd, 0, SEEK_SET) < 0) {
        gnutls_hash_deinit(hd, nullptr);
        return false;
    }

    std::size_t pos = 0;
    while (pos < skip_offset) {
        const std::size_t chunk = std::min(kBuf, skip_offset - pos);
        const ssize_t n = ::read(fd, buf.data(), chunk);
        if (n <= 0) {
            gnutls_hash_deinit(hd, nullptr);
            return false;
        }
        gnutls_hash(hd, buf.data(), static_cast<std::size_t>(n));
        pos += static_cast<std::size_t>(n);
    }

    const unsigned char zero = 0;
    for (std::size_t i = 0; i < skip_length; ++i) {
        gnutls_hash(hd, &zero, 1);
    }

    if (lseek(fd, static_cast<off_t>(skip_offset + skip_length), SEEK_SET) < 0) {
        gnutls_hash_deinit(hd, nullptr);
        return false;
    }

    ssize_t n = 0;
    while ((n = ::read(fd, buf.data(), kBuf)) > 0) {
        gnutls_hash(hd, buf.data(), static_cast<std::size_t>(n));
    }

    gnutls_hash_output(hd, out_digest);
    gnutls_hash_deinit(hd, nullptr);
    return true;
}

class GpgmeContext {
public:
    GpgmeContext() {
        // Returns the library version, or null if initialisation failed.
        if (!gpgme_check_version(nullptr)) {
            ok_ = false;
            return;
        }
        gpgme_set_locale(nullptr, 0, "C");
        ok_ = (gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP) == 0);
    }

    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

// The embedded key is imported into a home of its own: the root helper's
// keyring never holds it. no-autostart keeps gpg from leaving an agent
// running for every connection.
class ScratchGpgHome {
public:
    ScratchGpgHome() {
        char tmpl[] = "/tmp/rpi-imager-gpg-XXXXXX";
        if (!::mkdtemp(tmpl)) {
            return;
        }
        path_ = tmpl;
        const std::string conf = path_ + "/gpg.conf";
        const int fd = ::open(conf.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        static constexpr char kConf[] = "no-autostart\n";
        ok_ = fd >= 0 && ::write(fd, kConf, sizeof(kConf) - 1) == sizeof(kConf) - 1;
        if (fd >= 0) {
            ::close(fd);
        }
    }
    ~ScratchGpgHome() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }
    ScratchGpgHome(const ScratchGpgHome&) = delete;
    ScratchGpgHome& operator=(const ScratchGpgHome&) = delete;

    bool ok() const { return ok_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    bool ok_ = false;
};

struct GpgmeCtxDeleter {
    void operator()(gpgme_ctx_t c) const { gpgme_release(c); }
};
struct GpgmeDataDeleter {
    void operator()(gpgme_data_t d) const { gpgme_data_release(d); }
};
using GpgmeCtx = std::unique_ptr<std::remove_pointer_t<gpgme_ctx_t>, GpgmeCtxDeleter>;
using GpgmeData = std::unique_ptr<std::remove_pointer_t<gpgme_data_t>, GpgmeDataDeleter>;

GpgmeData dataFrom(const void* bytes, std::size_t len) {
    gpgme_data_t d = nullptr;
    if (gpgme_data_new_from_mem(&d, static_cast<const char*>(bytes), len, 1) != 0) {
        return nullptr;
    }
    return GpgmeData(d);
}

bool fingerprintAllowed(const std::string& fp,
                        const std::vector<std::string>& trusted) {
    if (trusted.empty()) {
        return true;
    }
    const std::string norm = normalizeFingerprint(fp);
    for (const auto& t : trusted) {
        if (norm == normalizeFingerprint(t)) {
            return true;
        }
    }
    return false;
}

} // namespace

const char* verifyResultMessage(VerifyResult r) {
    switch (r) {
        case VerifyResult::Ok: return "ok";
        case VerifyResult::Unsigned: return "unsigned AppImage";
        case VerifyResult::Tampered: return "signature invalid or digest mismatch";
        case VerifyResult::UntrustedKey: return "signer key not in trust list";
        case VerifyResult::IoError: return "I/O error";
        case VerifyResult::InternalError: return "internal verification error";
    }
    return "unknown";
}

bool findElfSection(const std::string& path, const char* section_name, SectionInfo& out) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    ElfIdent ident {};
    if (!parseIdent(fd, ident)) {
        ::close(fd);
        return false;
    }

    unsigned char ehdr[64] = {0};
    const std::size_t ehdr_size = ident.ei_class == 1 ? 52 : 64;
    if (!readAt(fd, 0, ehdr, ehdr_size)) {
        ::close(fd);
        return false;
    }

    const std::size_t shoff = ident.ei_class == 1
                                  ? readLe<std::uint32_t>(ehdr + 32)
                                  : readLe<std::uint64_t>(ehdr + 40);
    const std::uint16_t shentsize = ident.ei_class == 1
                                        ? readLe<std::uint16_t>(ehdr + 46)
                                        : readLe<std::uint16_t>(ehdr + 58);
    const std::uint16_t shnum = ident.ei_class == 1
                                    ? readLe<std::uint16_t>(ehdr + 48)
                                    : readLe<std::uint16_t>(ehdr + 60);
    const std::uint16_t shstrndx = ident.ei_class == 1
                                       ? readLe<std::uint16_t>(ehdr + 50)
                                       : readLe<std::uint16_t>(ehdr + 62);

    if (shnum == 0 || shentsize < 40) {
        ::close(fd);
        return false;
    }

    std::vector<unsigned char> shstrtab;
    {
        const std::size_t shstr_off = shoff + static_cast<std::size_t>(shstrndx) * shentsize;
        unsigned char shdr[64] = {0};
        if (!readAt(fd, static_cast<off_t>(shstr_off), shdr, shentsize)) {
            ::close(fd);
            return false;
        }
        const std::size_t str_off = ident.ei_class == 1
                                        ? readLe<std::uint32_t>(shdr + 16)
                                        : readLe<std::uint64_t>(shdr + 24);
        const std::size_t str_size = ident.ei_class == 1
                                         ? readLe<std::uint32_t>(shdr + 20)
                                         : readLe<std::uint64_t>(shdr + 32);
        if (str_size == 0 || str_size > 16 * 1024 * 1024) {
            ::close(fd);
            return false;
        }
        shstrtab.resize(str_size);
        if (!readAt(fd, static_cast<off_t>(str_off), shstrtab.data(), str_size)) {
            ::close(fd);
            return false;
        }
    }

    for (std::uint16_t i = 0; i < shnum; ++i) {
        const std::size_t ent_off = shoff + static_cast<std::size_t>(i) * shentsize;
        unsigned char shdr[64] = {0};
        if (!readAt(fd, static_cast<off_t>(ent_off), shdr, shentsize)) {
            continue;
        }

        const std::uint32_t name_off = readLe<std::uint32_t>(shdr);
        if (name_off >= shstrtab.size()) {
            continue;
        }
        const char* name = reinterpret_cast<const char*>(shstrtab.data() + name_off);
        if (std::strcmp(name, section_name) != 0) {
            continue;
        }

        out.offset = ident.ei_class == 1
                         ? readLe<std::uint32_t>(shdr + 16)
                         : readLe<std::uint64_t>(shdr + 24);
        out.length = ident.ei_class == 1
                         ? readLe<std::uint32_t>(shdr + 20)
                         : readLe<std::uint64_t>(shdr + 32);
        ::close(fd);
        return out.length > 0;
    }

    ::close(fd);
    return false;
}

VerifyResult verifyEmbeddedSignature(
    const std::string& appimage_path,
    const std::vector<std::string>& trusted_key_fingerprints) {
    SectionInfo sig {};
    SectionInfo key {};
    if (!findElfSection(appimage_path, kSigSection, sig) || sig.length == 0) {
        return VerifyResult::Unsigned;
    }
    (void)findElfSection(appimage_path, kKeySection, key);

    const std::size_t skip_offset = sig.offset;
    std::size_t skip_length = sig.length;
    if (key.length > 0 && key.offset == sig.offset + sig.length) {
        skip_length += key.length;
    }

    const int fd = ::open(appimage_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return VerifyResult::IoError;
    }

    unsigned char digest[32] = {0};
    if (!sha256FileZeroedSections(fd, skip_offset, skip_length, digest)) {
        ::close(fd);
        return VerifyResult::InternalError;
    }
    ::close(fd);

    char digest_hex[65] = {0};
    for (int i = 0; i < 32; ++i) {
        std::snprintf(digest_hex + (i * 2), 3, "%02x", digest[i]);
    }

    std::vector<unsigned char> signature(sig.length);
    {
        const int sfd = ::open(appimage_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (sfd < 0 || !readAt(sfd, static_cast<off_t>(sig.offset), signature.data(), sig.length)) {
            if (sfd >= 0) {
                ::close(sfd);
            }
            return VerifyResult::IoError;
        }
        ::close(sfd);
    }

    // Trim trailing NUL padding in the signature section.
    while (!signature.empty() && signature.back() == 0) {
        signature.pop_back();
    }
    if (signature.empty()) {
        return VerifyResult::Unsigned;
    }

    std::vector<unsigned char> public_key(key.length);
    if (key.length > 0) {
        const int kfd = ::open(appimage_path.c_str(), O_RDONLY | O_CLOEXEC);
        const bool read_ok = kfd >= 0
            && readAt(kfd, static_cast<off_t>(key.offset), public_key.data(), key.length);
        if (kfd >= 0) {
            ::close(kfd);
        }
        if (!read_ok) {
            return VerifyResult::IoError;
        }
    }
    while (!public_key.empty() && public_key.back() == 0) {
        public_key.pop_back();
    }
    if (public_key.empty()) {
        return VerifyResult::UntrustedKey;
    }

    static GpgmeContext gpg_init;
    if (!gpg_init.ok()) {
        return VerifyResult::InternalError;
    }

    ScratchGpgHome home;
    gpgme_ctx_t raw_ctx = nullptr;
    if (!home.ok() || gpgme_new(&raw_ctx) != 0) {
        return VerifyResult::InternalError;
    }
    const GpgmeCtx ctx(raw_ctx);
    gpgme_set_protocol(ctx.get(), GPGME_PROTOCOL_OpenPGP);
    if (gpgme_ctx_set_engine_info(ctx.get(), GPGME_PROTOCOL_OpenPGP, nullptr,
                                  home.path().c_str()) != 0) {
        return VerifyResult::InternalError;
    }

    const GpgmeData key_data = dataFrom(public_key.data(), public_key.size());
    if (!key_data) {
        return VerifyResult::InternalError;
    }
    const gpgme_import_result_t imported =
        gpgme_op_import(ctx.get(), key_data.get()) == 0 ? gpgme_op_import_result(ctx.get())
                                                        : nullptr;
    if (!imported || imported->imported + imported->unchanged == 0) {
        return VerifyResult::Tampered;
    }

    const GpgmeData sig_data = dataFrom(signature.data(), signature.size());
    const GpgmeData signed_digest = dataFrom(digest_hex, std::strlen(digest_hex));
    if (!sig_data || !signed_digest) {
        return VerifyResult::InternalError;
    }

    // Detached: the signed text is the third argument, not the fourth.
    if (gpgme_op_verify(ctx.get(), sig_data.get(), signed_digest.get(), nullptr) != 0) {
        return VerifyResult::Tampered;
    }

    // gpgme fills in fpr from the signature packet even when the signature is
    // bad or its key unknown, so the status has to be checked before it.
    const gpgme_verify_result_t result = gpgme_op_verify_result(ctx.get());
    const gpgme_signature_t sig_result = result ? result->signatures : nullptr;
    if (!sig_result || gpgme_err_code(sig_result->status) != GPG_ERR_NO_ERROR
        || !sig_result->fpr) {
        return VerifyResult::Tampered;
    }

    // A signing subkey reports its own fingerprint; pins name the primary key.
    std::string primary = sig_result->fpr;
    gpgme_key_t signer = nullptr;
    if (gpgme_get_key(ctx.get(), sig_result->fpr, &signer, 0) == 0 && signer) {
        if (signer->fpr) {
            primary = signer->fpr;
        }
        gpgme_key_unref(signer);
    }
    if (!fingerprintAllowed(primary, trusted_key_fingerprints)) {
        return VerifyResult::UntrustedKey;
    }
    return VerifyResult::Ok;
}

} // namespace rpi_imager::appimage
