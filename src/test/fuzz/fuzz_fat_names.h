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
        // A NUL and nothing narrower. The 8.3 name now stops at every byte
        // below 0x20, but a *long* filename is assembled from UCS-2 the
        // directory chose, and what such a name should do about a control
        // character is a decision nobody has made -- reject the entry, strip
        // it, or pass it on. Asserting the stricter rule here would hold the
        // code to a contract it does not have.
        if (n.contains(QChar(u'\0')))
            __builtin_trap();           // read past the terminator
        const QStringList parts = n.split(QLatin1Char('/'));
        for (const QString &part : parts)
            if (part == QLatin1String(".."))
                __builtin_trap();       // a path out of the tree
    }
}

} // namespace fuzz
