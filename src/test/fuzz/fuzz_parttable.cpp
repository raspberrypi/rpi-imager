// Fuzz the partition table handling in front of the FAT driver.
//
// fatPartition() reads an MBR at sector 0 or a GPT header at LBA 1, and for
// GPT the location, count and stride of the entry array come out of that
// header. Its arithmetic is guarded by hand, and the offset goes straight to
// the FAT parser: a table surviving the checks chooses where it reads.
//
// The fuzzer owns the first 64 KiB. Behind it sits a real filesystem from
// FUZZ_FAT_TEMPLATE -- without one the parser met zeroes, refused on its
// first field, and none of this was reached.

#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "file_operations.h"
#include "fuzz_fat_names.h"
#include "fuzz_silence.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

constexpr size_t kTableArea = 64 * 1024;

// What make_seeds.py puts in the template's config.txt. Written out here
// rather than read from the image, so a harness that pointed at the wrong
// place could not agree with itself.
const QByteArray kTemplateConfigTxt =
    QByteArrayLiteral("dtparam=audio=on\ndtoverlay=vc4-kms-v3d\narm_64bit=1\n");

std::vector<uint8_t> g_template;
int g_fd = -1;
size_t g_imageSize = 0;

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
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fprintf(stderr, "%s is empty\n", templatePath());
        _exit(1);
    }
    g_template.resize(size_t(n));
    if (std::fread(g_template.data(), 1, g_template.size(), f) != g_template.size())
        _exit(1);
    std::fclose(f);

    // Written once. Only the table area is rewritten per case, the same way
    // fuzz_fatdir treats its image: rebuilding the whole thing each time is
    // memcpy and write(), not parsing.
    g_imageSize = kTableArea + g_template.size();
    g_fd = ::memfd_create("parttable", 0);
    if (g_fd < 0)
        _exit(1);
    std::vector<uint8_t> zeros(kTableArea, 0);
    if (::pwrite(g_fd, zeros.data(), zeros.size(), 0) != ssize_t(zeros.size()))
        _exit(1);
    if (::pwrite(g_fd, g_template.data(), g_template.size(), off_t(kTableArea))
            != ssize_t(g_template.size()))
        _exit(1);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 16 || size > kTableArea || g_fd < 0)
        return 0;

    // Clear the whole table area before laying the case over it, or a short
    // input inherits the tail of the last one.
    std::vector<uint8_t> table(kTableArea, 0);
    std::memcpy(table.data(), data, std::min(size, kTableArea));
    if (::pwrite(g_fd, table.data(), table.size(), 0) != ssize_t(table.size()))
        return 0;

    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/fd/%d", g_fd);

    auto ops = rpi_imager::FileOperations::Create();
    if (ops->OpenDevice(path) == rpi_imager::FileError::kSuccess) {
        DeviceWrapper dw(ops.get());
        // Every partition the API admits, including the ones it refuses.
        for (int nr = 0; nr <= 5; ++nr) {
            try {
                DeviceWrapperFatPartition *fat = dw.fatPartition(nr);
                if (fat) {
                    // Reached when the table and the FAT boot sector behind it
                    // were both accepted -- which needs a table pointing at
                    // the template, 64 KiB in.
                    (void)fat->fileExists(QStringLiteral("config.txt"));
                    fuzz::checkFatNames(fat->listAllFiles());

                    // The point of the table is to say where the filesystem
                    // starts, and the offset it yields goes straight to the
                    // parser. There is one real filesystem in this image and
                    // its config.txt is known, so a table that survived every
                    // check and then read something else chose the wrong
                    // place -- which on a card is a partition written over
                    // whatever was next to it.
                    //
                    // Empty is the ordinary answer and says nothing: the
                    // fuzzer's own 64 KiB can look enough like a boot sector
                    // to be accepted, and there is no config.txt in it.
                    const QByteArray got = fat->readFile(QStringLiteral("config.txt"));
                    if (!got.isEmpty() && got != kTemplateConfigTxt)
                        __builtin_trap();
                }
            } catch (const std::exception &) {
                // Refusing a malformed table is the correct outcome.
            } catch (...) {
            }
        }
    }
    return 0;
}
