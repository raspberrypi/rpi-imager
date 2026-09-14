// Fuzz the archive-size parsers the downloader runs over what it fetched.
//
// These read an .xz footer, a .gz ISIZE field and zstd frame headers to
// learn how large the image will be once unpacked. The bytes come off the
// network, and the xz path in particular seeks backwards by a length the
// footer itself supplies -- so the file chooses where the parser reads.
#include "imagesizeparser.h"
#include "fuzz_silence.h"

#include <QString>

#include <cstdint>
#include <cstdio>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 1024 * 1024)
        return 0;

    int fd = ::memfd_create("archive", 0);
    if (fd < 0)
        return 0;
    if (size && ::write(fd, data, size) != ssize_t(size)) {
        ::close(fd);
        return 0;
    }

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    const QString qpath = QString::fromLatin1(path);

    // Every entry point, because which one runs is chosen by a file name
    // elsewhere and a mislabelled file reaches the wrong parser.
    (void)imagesize::parseXz(qpath);
    (void)imagesize::parseGz(qpath);
    (void)imagesize::parseZstd(qpath);
    const imagesize::ArchiveInfo info = imagesize::parseArchive(qpath);
    (void)info.uncompressedSize;

    // What the file *is*, which is the decision the write path makes too:
    // get it wrong and the card is given the zip rather than what is inside
    // it, or a mislabelled .img is written decompressed. Both are chosen
    // from these bytes, so both belong here.
    const imagesize::SourceFormat probed = imagesize::probeFormat(qpath);
    (void)probed.format;
    (void)probed.filterCode;

    const imagesize::SourceSize measured = imagesize::measureLocalFile(qpath);

    // A size called reliable is one the caller will divide by. Zero is not
    // one of those.
    //
    // Not the other way round: a non-zero size behind a false flag is the
    // deliberate middle state, and gzip is the reason for it -- ISIZE is the
    // original size modulo 2^32, good enough to refuse a card that is
    // plainly too small and never good enough for progress. Asserting the
    // converse here found only that, on the first gzip it was given.
    if (measured.sizeIsReliable && measured.uncompressedSize == 0)
        __builtin_trap();

    // Nothing has a negative count of files, and nothing unreadable has any.
    if (measured.fileCount < 0 || info.fileCount < 0)
        __builtin_trap();
    if (!probed.readable && measured.fileCount != 0)
        __builtin_trap();

    ::close(fd);
    return 0;
}
