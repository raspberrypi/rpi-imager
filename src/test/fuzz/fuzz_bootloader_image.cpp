// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// BootloaderImage is the second of the two EEPROM readers in this tree. It
// parses pieeprom.original.bin -- a firmware image fetched over the network
// -- and every offset it works from is a length read out of that file. What
// it produces is counter-signed and written to the EEPROM of a Compute
// Module that will accept one image.
//
// load() insists on exactly 512 KiB or 2 MiB, so the harness keeps a memfd
// of the smaller size and overwrites the head of it per input rather than
// building a file each time.

#include "rpiboot/bootloader_image.h"
#include "fuzz_silence.h"

#include <QByteArray>
#include <QString>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr size_t kImageSize = 512 * 1024;

int imageFd()
{
    static int fd = [] {
        const int f = memfd_create("eeprom", 0);
        if (f < 0)
            return -1;
        // 0xff is unwritten flash, which is what parse() treats as the end.
        const std::vector<uint8_t> blank(kImageSize, 0xff);
        if (pwrite(f, blank.data(), blank.size(), 0) != ssize_t(blank.size()))
            return -1;
        return f;
    }();
    return fd;
}

// How much of the image the previous input dirtied, so only that much has to
// be wiped back to 0xff. Without it an input inherits the tail of the last
// one and a reported crash does not reproduce on its own.
size_t g_dirty = 0;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const int fd = imageFd();
    if (fd < 0)
        return 0;

    const size_t n = size < kImageSize ? size : kImageSize;
    if (g_dirty > n) {
        const std::vector<uint8_t> blank(g_dirty - n, 0xff);
        if (pwrite(fd, blank.data(), blank.size(), off_t(n)) < 0)
            return 0;
    }
    if (n && pwrite(fd, data, n, 0) != ssize_t(n))
        return 0;
    g_dirty = n;

    const QString path = QStringLiteral("/proc/self/fd/%1").arg(fd);

    rpiboot::BootloaderImage img;
    if (!img.load(path))
        return 0;

    // Reading: the paths the provisioner takes before it signs anything.
    (void)img.isABImage();
    for (const char *name : {"bootsys", "bootcode.bin", "bootconf.txt",
                             "bootconf.sig", "pubkey.bin", ""}) {
        const QByteArray got = img.getFile(QString::fromLatin1(name));
        // A section can never hand back more than the image it lives in.
        if (got.size() > int(kImageSize))
            __builtin_trap();
    }

    // Writing: updateFile and friends compute pad offsets from the same
    // lengths, so they are where an out-of-bounds write would be. save() is
    // never called -- nothing here touches the filesystem.
    const size_t payloadLen = size ? (data[size - 1] * 37u) % 8192u : 0u;
    const QByteArray payload(int(payloadLen), '\x5A');

    // A property, not just a crash hunt: what updateFile() accepts, getFile()
    // has to give back. Both work from the recorded section length, so a
    // disagreement between them is the same class of defect as the negative
    // length above -- and it would otherwise show up as an EEPROM written
    // with something other than what was asked for.
    if (img.updateFile(QStringLiteral("bootconf.txt"), payload)) {
        if (img.getFile(QStringLiteral("bootconf.txt")) != payload)
            __builtin_trap();
    }
    (void)img.updateBootcode(payload);
    (void)img.updateBootsys(payload);

    // Whatever the edits did, the image keeps the size the part expects.
    if (img.bytes().size() != int(kImageSize))
        __builtin_trap();

    return 0;
}
