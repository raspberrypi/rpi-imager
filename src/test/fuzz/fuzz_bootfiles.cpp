// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Fuzz the TAR reader that unpacks a downloaded firmware bundle.
//
// bootfiles.bin arrives inside an rpi-eeprom release and holds the bootcode
// for a Compute Module. Every length and name in it comes off the wire.
//
// This target exists because reading found what fuzzing had not:
// extractFromArchive() sized its buffer from archive_entry_size() before
// reading the entry, so a header claiming a large size got it. Fixed; this
// keeps it fixed and covers the rest of the walk.

#include "rpiboot/bootfiles.h"
#include "fuzz_silence.h"

#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Larger than any real bundle, and enough to build a header that lies.
    if (size > 4 * 1024 * 1024)
        return 0;

    const std::vector<uint8_t> tar(data, data + size);

    rpiboot::Bootfiles bf;
    if (!bf.extractFromMemory(tar)) {
        // Refusing a malformed archive is the right answer, and lastError()
        // is built from the entry name, so it is worth touching.
        (void)bf.lastError().size();
        return 0;
    }

    const size_t before = bf.files().size();

    // The lookups the firmware manager performs: a chip-prefixed path first,
    // then the bare name at the root.
    //
    // The bound is the archive, not the harness's input cap. Only
    // archive_read_support_filter_none() is enabled, so nothing here is
    // decompressed and an entry's content is a byte range inside the bytes
    // handed in -- it cannot be longer than them. The check used to read
    // `> 4 MiB`, which is the cap itself, so three megabytes out of a
    // fifty-byte archive satisfied it. That is the defect this target was
    // written for: a header claiming a size, believed before it was read.
    for (const char *name : {"bootcode4.bin", "bootcode5.bin", "config.txt", ""}) {
        const auto *d = bf.find(name);
        if (d && d->size() > size)
            __builtin_trap();   // never more than the archive it came from
        for (const char *prefix : {"2711", "2712", "", "../.."}) {
            const auto *p = bf.find(name, prefix);
            if (p && p->size() > size)
                __builtin_trap();
        }
    }

    // And the same bound over the whole map rather than four names: a tar
    // stores each entry once, after a header, so every entry together still
    // fits in the archive. An over-read spread across entries nobody looked
    // up would pass every check above.
    size_t total = 0;
    for (const auto &f : bf.files()) {
        if (f.second.size() > size)
            __builtin_trap();
        total += f.second.size();
    }
    if (total > size)
        __builtin_trap();       // more content than the archive holds

    // replaceEntry() splices a counter-signed bootcode into what the device
    // is served, and no harness had called it. It is the one write path
    // here, it takes a name off the archive, and what it stores is what the
    // device gets.
    if (!bf.files().empty()) {
        const std::string name = bf.files().begin()->first;
        const std::vector<uint8_t> body(1 + (size % 97), 0x5a);
        if (!bf.replaceEntry(name, body))
            __builtin_trap();   // refused a name it had just listed
        const auto *back = bf.find(name);
        if (!back || *back != body)
            __builtin_trap();   // spliced, and served as something else
        if (bf.files().size() != before)
            __builtin_trap();   // a replacement that added an entry
    }

    // A name the archive does not hold must be refused, and refusing it must
    // leave the map alone -- the header says to use writeToFile for new
    // entries, so an insert here would be a silent extra file served.
    const std::string absent = "\x01\x02no-such-entry";
    const size_t count = bf.files().size();
    if (!bf.find(absent)) {     // an archive really can name it; then skip
        if (bf.replaceEntry(absent, {0x00}))
            __builtin_trap();   // inserted where it said it would not
        if (bf.files().size() != count)
            __builtin_trap();
    }

    return 0;
}
