// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Fuzz the ASN.1 length reader on its own, rather than only through a key.
//
// fuzz_der reaches it, but only with bytes shaped like a
// SubjectPublicKeyInfo -- the mutator is working towards a valid key, and
// the interesting lengths are the ones no key would carry. This hands it
// arbitrary bytes and an arbitrary offset.
//
// It is worth its own target because it had a real defect and the caller
// cannot defend itself against it: four length bytes can say more than an
// int holds, so shifting one in wrapped negative, and `i + algoLen > dlen`
// is int arithmetic that overflowed rather than failing. Eight bytes
// segfaulted it. The same overflow was written twice, here and in the macOS
// backend, which is why the function is shared.
#include "asn1_length.h"
#include "fuzz_silence.h"

#include <climits>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size > 64 * 1024)
        return 0;

    // The first two bytes choose where to start reading, including offsets
    // outside the buffer, which the caller is allowed to pass.
    const int len = static_cast<int>(size - 2);
    const uint8_t *d = data + 2;
    int off = static_cast<int>((uint16_t(data[0]) << 8 | data[1]) % (size + 8));
    const int startedAt = off;

    const int result = rpi_imager::asn1ParseLength(d, len, &off);

    if (result == -1) {
        // A refusal must not move the cursor backwards. It may leave it past
        // the buffer, but only where the caller put it there: handed an
        // offset already past the end, the function refuses and touches
        // nothing, which is the right answer and not a promise to repair the
        // caller's arithmetic. Asserting otherwise was this harness's first
        // property and it was wrong -- found in a minute, by an offset of
        // seven into one byte.
        if (off < startedAt)
            __builtin_trap();
        if (startedAt <= len && off > len)
            __builtin_trap();
        return 0;
    }

    // The contract the header states: a length that is not negative and not
    // longer than what is left. Both halves matter -- the first is the
    // overflow that wrapped, the second is what makes `i + algoLen > dlen`
    // safe at the call sites.
    if (result < 0)
        __builtin_trap();
    if (off < startedAt || off > len)
        __builtin_trap();
    if (result > len - off)
        __builtin_trap();

    // And the cursor moved: a length read that consumed nothing would leave
    // a caller looping on the same byte for ever.
    if (off == startedAt)
        __builtin_trap();

    return 0;
}
