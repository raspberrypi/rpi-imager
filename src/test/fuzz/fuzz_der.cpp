// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Fuzz the ASN.1 reader that turns a public key into the boot ROM's format.
//
// The blob is openssl's output today, so this is defensive parsing -- but
// it is hand-rolled ASN.1, every length is a number read from the input,
// and it had no coverage at all.
//
// That showed. asn1ParseLength() returned whatever four length bytes said,
// so a length near INT_MAX overflowed the caller's own bounds check, passed
// it, and left the cursor negative. Eight bytes segfaulted it.
//
// Fixed. This keeps it fixed and covers the rest: the nested SEQUENCEs, the
// BIT STRING, the sign-byte strip, the exponent.

#include "secureboot_crypto.h"
#include "fuzz_silence.h"

#include <QByteArray>

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // A SubjectPublicKeyInfo for a 2048-bit key is about 294 bytes; anything
    // beyond a few kilobytes is only slowing the loop down.
    if (size > 8 * 1024)
        return 0;

    const QByteArray der(reinterpret_cast<const char *>(data), int(size));
    const QByteArray out = SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(der);

    // The boot ROM format is fixed: 256 bytes of modulus then 8 of exponent.
    // Structural only -- the parser builds the result to that size -- but it
    // is cheap and it pins the shape the boot ROM is promised.
    if (!out.isEmpty() && out.size() != 264)
        __builtin_trap();

    // Second pass: the success path, which random bytes reach but never with
    // a modulus we know. Wrap the input in a well-formed SubjectPublicKeyInfo
    // and the answer is known in advance, so byte order, the sign-byte strip
    // and the exponent padding are all checked rather than assumed. Getting
    // any of them wrong fuses a key that is not the one on disk.
    if (size < 257)
        return 0;

    const uint8_t *n = data;                       // 256 bytes of modulus
    const int eLen = 1 + (data[256] & 7);          // 1..8 bytes of exponent
    if (size < size_t(257 + eLen))
        return 0;
    const uint8_t *e = data + 257;

    QByteArray der2;
    auto appendLen = [&der2](int len) {
        if (len < 0x80) {
            der2.append(char(len));
        } else {
            der2.append(char(0x82));
            der2.append(char((len >> 8) & 0xff));
            der2.append(char(len & 0xff));
        }
    };

    // INTEGER N, with the 0x00 sign byte DER prepends to keep it positive.
    QByteArray intN;
    intN.append(char(0x02));
    {
        QByteArray tmp;
        tmp.append(char(0x82));
        tmp.append(char(0x01));
        tmp.append(char(0x01));                    // 257
        intN.append(tmp);
    }
    intN.append(char(0x00));
    intN.append(reinterpret_cast<const char *>(n), 256);

    QByteArray intE;
    intE.append(char(0x02));
    intE.append(char(eLen));
    intE.append(reinterpret_cast<const char *>(e), eLen);

    QByteArray rsaSeq;
    rsaSeq.append(char(0x30));
    {
        QByteArray body = intN + intE;
        QByteArray saved = der2;
        der2.clear();
        appendLen(body.size());
        rsaSeq.append(der2);
        der2 = saved;
        rsaSeq.append(body);
    }

    QByteArray bits;
    bits.append(char(0x03));
    {
        QByteArray saved = der2;
        der2.clear();
        appendLen(rsaSeq.size() + 1);
        bits.append(der2);
        der2 = saved;
    }
    bits.append(char(0x00));                       // no unused bits
    bits.append(rsaSeq);

    // AlgorithmIdentifier for rsaEncryption, which the parser skips whole.
    static const unsigned char kAlgo[] = {
        0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
        0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00
    };
    QByteArray algo(reinterpret_cast<const char *>(kAlgo), int(sizeof(kAlgo)));

    QByteArray spki;
    spki.append(char(0x30));
    {
        QByteArray saved = der2;
        der2.clear();
        appendLen(algo.size() + bits.size());
        spki.append(der2);
        der2 = saved;
    }
    spki.append(algo);
    spki.append(bits);

    const QByteArray got = SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(spki);
    if (got.size() != 264)
        __builtin_trap();          // a key openssl would emit was rejected

    for (int j = 0; j < 256; ++j)
        if (uint8_t(got[j]) != n[255 - j])
            __builtin_trap();      // modulus not reversed byte for byte

    for (int j = 0; j < 8; ++j) {
        const uint8_t want = j < eLen ? e[eLen - 1 - j] : 0;
        if (uint8_t(got[256 + j]) != want)
            __builtin_trap();      // exponent reversed or padded wrongly
    }

    return 0;
}
