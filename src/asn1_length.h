/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#ifndef ASN1_LENGTH_H
#define ASN1_LENGTH_H

#include <climits>
#include <cstdint>

namespace rpi_imager {

/*
 * Read one ASN.1 DER length at *off, advance past it, return it, or -1.
 *
 * Shared because it was written twice -- here and in the macOS backend --
 * with the same overflow in both. Four length bytes can say more than an int
 * holds, so shifting one in wraps negative; and a length longer than what is
 * left cannot be honest. Bounding it here is what makes the call sites safe:
 * `i + algoLen > dlen` is int arithmetic and overflowed instead of failing.
 *
 * The bound covers the short form too: one copy checked it, one did not.
 */
inline int asn1ParseLength(const uint8_t *d, int len, int *off)
{
    if (!d || !off || len < 0 || *off < 0 || *off >= len)
        return -1;

    int result = 0;
    const uint8_t first = d[(*off)++];
    if (first < 0x80) {
        result = first;
    } else {
        const int count = first & 0x7f;
        if (count == 0 || count > 4 || *off + count > len)
            return -1;
        for (int i = 0; i < count; ++i) {
            if (result > (INT_MAX >> 8))
                return -1;
            result = (result << 8) | d[(*off)++];
        }
    }

    if (result < 0 || result > len - *off)
        return -1;
    return result;
}

} // namespace rpi_imager

#endif // ASN1_LENGTH_H
