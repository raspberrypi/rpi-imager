// Fuzz the imager's own FAT driver against a corrupted directory region.
//
// This code reads whatever card is inserted, so its input is as untrusted as
// anything gets: cluster chains, long-filename chains and directory entries
// are all attacker-shaped. Mutating a whole 32 MiB image would spend the
// budget failing boot-sector checks, so a valid template is held fixed and
// only the root directory and the FAT are overwritten from the fuzz input.
#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "file_operations.h"
#include "fuzz_fat_names.h"
#include "fuzz_silence.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

// Geometry of the template, read once at start-up.
constexpr size_t kBootOff  = 0;
constexpr size_t kBootBytes = 512;
constexpr size_t kFatOff   = 2048;
constexpr size_t kFatBytes = 65536;
constexpr size_t kDirOff   = 67584;
constexpr size_t kDirBytes = 16384;

std::vector<uint8_t> g_template;

// The image is written once and only the fuzzed regions are rewritten per
// case. Rebuilding all 32 MiB each time held the target to about eleven
// executions a second, and almost all of that was memcpy and write() rather
// than anything in the parser.
int g_fd = -1;

// Set when an iteration wrote to the image. writeFile() and deleteFile()
// touch clusters anywhere, not only the two fuzzed regions, so the next case
// has to start from the template again or it inherits the last one's edits
// and a reported crash does not reproduce on its own. Read-only cases, which
// are most of them, keep paying only for the two regions below.
bool g_dirty = false;

// Separately tracked: a case that fuzzed the boot sector has to put the
// template's back before the next one, or every later case inherits a broken
// geometry and refuses in the constructor.
bool g_bootDirty = false;

const char *templatePath()
{
    const char *p = ::getenv("FUZZ_FAT_TEMPLATE");
    return p ? p : "template.img";
}


} // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    FILE *f = std::fopen(templatePath(), "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", templatePath());
        _exit(1);
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    g_template.resize(size_t(n));
    if (std::fread(g_template.data(), 1, size_t(n), f) != size_t(n))
        _exit(1);
    std::fclose(f);

    g_fd = ::memfd_create("fatfuzz", 0);
    if (g_fd < 0)
        _exit(1);
    if (::write(g_fd, g_template.data(), g_template.size())
            != ssize_t(g_template.size()))
        _exit(1);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (g_template.empty() || g_fd < 0)
        return 0;

    // Restore the two regions from the template, then lay the input over
    // them: the FAT first, the root directory with whatever is left. Either
    // alone leaves half the parser untouched -- the chain walk lives in the
    // FAT, the name assembly in the directory.
    std::vector<uint8_t> fat(g_template.begin() + kFatOff,
                             g_template.begin() + kFatOff + kFatBytes);
    std::vector<uint8_t> dir(g_template.begin() + kDirOff,
                             g_template.begin() + kDirOff + kDirBytes);

    const size_t fatTake = size < kFatBytes ? size : kFatBytes;
    std::memcpy(fat.data(), data, fatTake);
    if (size > fatTake) {
        const size_t dirTake = std::min(size - fatTake, kDirBytes);
        std::memcpy(dir.data(), data + fatTake, dirTake);
    }

    // The boot sector, but only sometimes. Every field the constructor
    // validates comes from here -- bytes per sector, sectors per cluster, FAT
    // size -- and holding it at the template's valid values meant none of
    // those checks was ever reached. Fuzzing it on every case would be worse
    // than not fuzzing it: almost any mutation makes the constructor throw,
    // and the directory and chain walking below would stop being exercised at
    // all. One case in eight buys the checks without losing the depth.
    const bool fuzzBoot = size > 4 && (data[1] & 7) == 0;
    if (fuzzBoot || g_bootDirty) {
        std::vector<uint8_t> boot(g_template.begin() + kBootOff,
                                  g_template.begin() + kBootOff + kBootBytes);
        if (fuzzBoot) {
            const size_t take = std::min(size, kBootBytes);
            std::memcpy(boot.data(), data, take);
        }
        if (::pwrite(g_fd, boot.data(), boot.size(), off_t(kBootOff))
                != ssize_t(boot.size()))
            return 0;
        g_bootDirty = fuzzBoot;
    }

    if (g_dirty) {
        if (::pwrite(g_fd, g_template.data(), g_template.size(), 0)
                != ssize_t(g_template.size()))
            return 0;
        g_dirty = false;
    }

    if (::pwrite(g_fd, fat.data(), fat.size(), off_t(kFatOff))
            != ssize_t(fat.size()))
        return 0;
    if (::pwrite(g_fd, dir.data(), dir.size(), off_t(kDirOff))
            != ssize_t(dir.size()))
        return 0;

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/fd/%d", g_fd);

    auto ops = rpi_imager::FileOperations::Create();
    if (ops->OpenDevice(path) == rpi_imager::FileError::kSuccess) {
        try {
            DeviceWrapper dw(ops.get());
            DeviceWrapperFatPartition fat2(&dw, 0, g_template.size());
            (void)fat2.fileExists(QStringLiteral("config.txt"));
            (void)fat2.readFile(QStringLiteral("config.txt"));
            (void)fat2.readFile(QStringLiteral("a-very-long-filename-for-lfn-testing.conf"));
            (void)fat2.readFile(QStringLiteral("overlays/vc4-kms-v3d.dtbo"));
            (void)fat2.fileExists(QStringLiteral("nothing-here.txt"));

            // Walking the whole tree, which follows directory chains the
            // read calls above do not reach.
            fuzz::checkFatNames(fat2.listAllFiles());
            fuzz::checkFatNames(fat2.listAllFilesRecursive());

            // And the write path, which is what actually runs on a card:
            // downloadthread puts config.txt, cmdline.txt and firstrun.sh
            // onto the boot partition after the image is written. On a
            // damaged filesystem that means allocateCluster() and
            // updateDirEntry() working from the same corrupt structures the
            // read path just refused. Into the memfd copy, never a device.
            if (size && (data[0] & 1)) {
                g_dirty = true;

                // Sized from the input so that some cases stay inside one
                // cluster and others span several, which is the difference
                // between writing into an existing chain and extending it
                // through allocateCluster().
                const int len = 1 + (int(data[size - 1]) << 6);
                const QByteArray body(len, '\x41');

                // Everything else here is a crash hunt. A crash hunt cannot
                // see a write that reports success and lays the bytes down
                // wrongly, so a quarter of the write cases read them back.
                //
                // What this catches is an inconsistency: a short write, a
                // chain that cannot be walked again, a long name split one
                // way and joined another. What it cannot catch is a uniform
                // shift -- moving every cluster by the same amount moves the
                // read with it, and the bytes come back. Checked by doing
                // exactly that: seeking cluster-1 instead of cluster-2 runs
                // clean, while truncating a write by one byte trips it in
                // seconds. The sanitiser is what covers the other shape.
                //
                // Held to a quarter because reading back costs four fifths
                // of the execution rate, and the rest of this file wants the
                // executions.
                const bool readBack = (data[0] & 2) != 0;
                const auto write = [&](const QString &name) {
                    fat2.writeFile(name, body);
                    if (!readBack)
                        return;
                    if (fat2.readFile(name) != body)
                        __builtin_trap();   // written, then read as something else
                };

                write(QStringLiteral("config.txt"));
                write(QStringLiteral("cmdline.txt"));
                write(QStringLiteral("a-long-name-for-the-lfn-path.conf"));

                // Not a subdirectory write: writeFile() refuses those
                // outright, and the hundred-odd lines that would have done it
                // sit unreachable behind the throw. What this does reach is
                // the part before the refusal -- the name split, getDirEntry()
                // for the directory, and the ATTR_DIRECTORY check -- all of it
                // reading the same corrupt entries as the rest of this case.
                fat2.writeFile(QStringLiteral("overlays/fuzz.dtbo"), body);

                // The size a caller sees without reading the file back --
                // the same directory entry the read path just refused.
                (void)fat2.fileSize(QStringLiteral("config.txt"));
                (void)fat2.fileSize(QStringLiteral("overlays/fuzz.dtbo"));

                (void)fat2.deleteFile(QStringLiteral("config.txt"));
                (void)fat2.deleteFile(QStringLiteral("overlays/fuzz.dtbo"));
                (void)fat2.readFile(QStringLiteral("cmdline.txt"));
            }
        } catch (const std::exception &) {
        } catch (...) {
        }
    }
    return 0;
}
