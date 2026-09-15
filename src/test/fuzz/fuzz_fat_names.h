/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * What a FAT directory listing is allowed to hand back.
 *
 * Shared rather than copied: two harnesses reach the same listing by
 * different routes -- fuzz_fatdir corrupts the directory, fuzz_parttable
 * corrupts the table that says where the directory is -- and a rule written
 * out once in each is how the driver came to assemble its own short names in
 * ten places, none of them agreeing.
 */

#pragma once

#include <QChar>
#include <QLatin1Char>
#include <QLatin1String>
#include <QString>
#include <QStringList>

namespace fuzz {

// A long-filename chain is assembled from whatever the directory holds, so
// these are attacker-shaped strings, and something later joins them to a
// path -- SecureBoot lists through here and keys a map with what it gets.
// Three things a correct assembler cannot produce: an empty name, one
// holding a NUL (0x0000 ends a long filename, so a name past it was read
// past the terminator), and a ".." component, since the parent entry is one
// a lister skips and following it leaves the tree.
inline void checkFatNames(const QStringList &names)
{
    for (const QString &n : names) {
        if (n.isEmpty())
            __builtin_trap();           // a name nothing can open
        // No control character at all, which both halves now promise. The
        // 8.3 name stops at every byte below 0x20; the long name is
        // assembled from UCS-2 the directory chose and drops them, keeping
        // the name rather than rejecting the entry. This was a NUL and
        // nothing narrower while that decision was outstanding.
        for (const QChar c : n)
            if (c.unicode() < 0x20)
                __builtin_trap();       // a control character in a filename
        const QStringList parts = n.split(QLatin1Char('/'));
        for (const QString &part : parts)
            if (part == QLatin1String(".."))
                __builtin_trap();       // a path out of the tree
    }
}

} // namespace fuzz
