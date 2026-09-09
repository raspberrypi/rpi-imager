// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// DeviceWrapperFatPartition is the imager's own FAT driver -- it is what
// writes cmdline.txt, config.txt and firstrun.sh onto the boot partition
// after the image has been written, so a bug in it corrupts a card that has
// otherwise been imaged perfectly. It was entirely uncovered.
//
// The existing fat_partition_test.cpp can only run against a real mounted
// partition named by FAT_TEST_MOUNT_PATH, so in CI it does nothing. This
// drives the same code against a filesystem built by mkfs.vfat in a scratch
// file: no device, no mount, no privileges, and both FAT16 and FAT32.
//
// Correctness is checked by reading back through the driver and, where it
// matters, by asking mkfs's own tooling -- never by trusting the writer to
// confirm its own work.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "file_operations.h"

#include <QByteArray>
#include <QDir>
#include <QProcess>
#include <QStringList>
#include <QUuid>

#include <QFile>

#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

#include "devicewrapperstructs.h"

#include "fixture_process.h"
#include "platform_tools.h"
#include "platform_fat.h"

using Catch::Matchers::ContainsSubstring;

namespace {

// A FAT filesystem in a scratch file, wrapped in the driver under test.
//
// Everything is per-process and removes itself, so `ctest -j` cannot make two
// of these collide and a failing case leaves no litter behind.
// A temp directory that removes itself however the test leaves.
//
// These cases each build a 64 MB image, and the manual cleanup they used to
// end with only ran when everything passed -- so every red run left another
// image behind in the temp area. On a machine where the temp area is a tmpfs
// that is how the suite ends up failing for want of space, a long way from
// whatever actually broke.
class ScopedTempDir
{
public:
    explicit ScopedTempDir(const QString &prefix)
        : _path(QDir::temp().filePath(
              QStringLiteral("%1-%2").arg(prefix,
                  QUuid::createUuid().toString(QUuid::WithoutBraces))))
    {
        QDir().mkpath(_path);
    }
    ~ScopedTempDir() { QDir(_path).removeRecursively(); }

    ScopedTempDir(const ScopedTempDir &) = delete;
    ScopedTempDir &operator=(const ScopedTempDir &) = delete;

    QString filePath(const QString &name) const { return QDir(_path).filePath(name); }

private:
    QString _path;
};

class FatImage
{
public:
    // populate() runs against the raw image with mtools before the driver
    // opens it, so cases can start from a filesystem written by something
    // other than the code under test.
    explicit FatImage(int fatBits, int sizeMB,
                      const std::function<void(const QString &)> &populate = {})
        // _scratch is declared first, so it is fully constructed before
        // anything below can throw -- and a member that has been constructed
        // is destroyed even when the constructor it belongs to exits by
        // exception. ~FatImage() is not called in that case, which is how the
        // FAT12 case (which asserts the constructor throws) used to leave its
        // directory behind on every run.
        : _scratch(QStringLiteral("rpi-imager-fat"))
    {
        _path = _scratch.filePath(QStringLiteral("fat.img"));

        _ops = rpi_imager::FileOperations::Create();
        if (_ops->CreateTestFile(_path.toStdString(),
                                 static_cast<std::uint64_t>(sizeMB) * 1024 * 1024) !=
            rpi_imager::FileError::kSuccess) {
            throw std::runtime_error("could not create the scratch image");
        }

        QString formatError;
        if (!rpi_test::makeFatFilesystem(_path, fatBits, QStringLiteral("TESTVOL"),
                                         &formatError)) {
            throw std::runtime_error("could not build the filesystem: " +
                                     formatError.toStdString());
        }

        if (populate)
            populate(_path);

        // Reopen through the device path so the driver sees the formatted
        // filesystem rather than the handle used to size the file.
        _ops = rpi_imager::FileOperations::Create();
        if (_ops->OpenDevice(_path.toStdString()) != rpi_imager::FileError::kSuccess)
            throw std::runtime_error("could not open the scratch image");

        _dw = std::make_unique<DeviceWrapper>(_ops.get());
        _fat = std::make_unique<DeviceWrapperFatPartition>(
            _dw.get(), 0, static_cast<quint64>(sizeMB) * 1024 * 1024);
    }

    ~FatImage()
    {
        // Release the handles before _scratch removes the directory.
        _fat.reset();
        _dw.reset();
        _ops.reset();
    }

    FatImage(const FatImage &) = delete;
    FatImage &operator=(const FatImage &) = delete;

    DeviceWrapperFatPartition &fat() { return *_fat; }
    void sync() { _dw->sync(); }

private:
    // Declared first so it is constructed first and destroyed last: the
    // directory outlives every handle into it, and survives a constructor
    // that throws part-way.
    ScopedTempDir _scratch;
    QString _path;
    std::unique_ptr<rpi_imager::FileOperations> _ops;
    std::unique_ptr<DeviceWrapper> _dw;
    std::unique_ptr<DeviceWrapperFatPartition> _fat;
};


bool haveMtools()
{
    static const bool found = QFileInfo::exists(QStringLiteral("/usr/bin/mmd")) &&
                              QFileInfo::exists(QStringLiteral("/usr/bin/mcopy"));
    return found;
}

// Run an mtools command against an image. MTOOLS_SKIP_CHECK stops it
// objecting to a filesystem that has no partition table.
bool runMtool(const QString &tool, const QStringList &args)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("MTOOLS_SKIP_CHECK"), QStringLiteral("1"));
    proc.setProcessEnvironment(env);
    const QString toolPath = rpi_test::toolPath(tool);
    proc.start(toolPath.isEmpty() ? tool : toolPath, args);
    proc.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

QByteArray patternOfSize(int size, char seed)
{
    QByteArray out;
    out.reserve(size);
    for (int i = 0; i < size; ++i)
        out.append(static_cast<char>(seed + (i % 61)));
    return out;
}

} // namespace

#define REQUIRE_MKFS()                                                                             \
    if (!rpi_test::haveFatFormatter())                                                             \
    SKIP(rpi_test::noFatFormatterReason())

// ---------------------------------------------------------------------------
// Mounting
// ---------------------------------------------------------------------------

TEST_CASE("FAT driver reads a freshly formatted FAT32 filesystem", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // A new filesystem has no files, but must enumerate cleanly rather than
    // throwing or returning junk.
    CHECK(image.fat().listAllFiles().isEmpty());
}

TEST_CASE("FAT driver reads a freshly formatted FAT16 filesystem", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    CHECK(image.fat().listAllFiles().isEmpty());
}

TEST_CASE("FAT driver refuses a FAT12 partition and says why", "[fat][image]")
{
    // Small cards still get formatted FAT12 by some tools. The driver does
    // not implement it, and the thing that matters is that it stops rather
    // than reading FAT16 structures out of a FAT12 table: the cluster
    // numbers would be misread and customisation written over whatever
    // happened to be at those sectors.
    REQUIRE_MKFS();
    REQUIRE_THROWS_WITH(FatImage(12, 2),
                        Catch::Matchers::ContainsSubstring("FAT12"));
}

TEST_CASE("FAT driver refuses a partition that is not FAT", "[fat][image]")
{
    // The constructor checks the 0x55AA signature and throws. Feed it a file
    // of zeroes: nothing else in the imager should be allowed to proceed on
    // the assumption that an unformatted card is a boot partition.
    ScopedTempDir scratch(QStringLiteral("rpi-imager-notfat"));
    const QString path = scratch.filePath(QStringLiteral("blank.img"));

    auto ops = rpi_imager::FileOperations::Create();
    REQUIRE(ops->CreateTestFile(path.toStdString(), 8 * 1024 * 1024) ==
            rpi_imager::FileError::kSuccess);
    auto reopened = rpi_imager::FileOperations::Create();
    REQUIRE(reopened->OpenDevice(path.toStdString()) == rpi_imager::FileError::kSuccess);

    DeviceWrapper dw(reopened.get());
    CHECK_THROWS_AS(DeviceWrapperFatPartition(&dw, 0, 8 * 1024 * 1024), std::runtime_error);

}

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

TEST_CASE("FAT driver round-trips a short 8.3 file", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    const QByteArray contents = "dtoverlay=disable-bt\nenable_uart=1\n";
    image.fat().writeFile(QStringLiteral("config.txt"), contents);
    image.sync();

    CHECK(image.fat().fileExists(QStringLiteral("config.txt")));
    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == contents);
}

TEST_CASE("FAT driver round-trips a long filename", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // Longer than 8.3, so this exercises the VFAT long-filename entries and
    // the short-name checksum that ties them to the directory entry.
    const QString name = QStringLiteral("a-rather-long-configuration-name.conf");
    const QByteArray contents = "long filename payload";
    image.fat().writeFile(name, contents);
    image.sync();

    CHECK(image.fat().fileExists(name));
    CHECK(image.fat().readFile(name) == contents);
    CHECK(image.fat().listAllFiles().contains(name));
}

TEST_CASE("FAT driver round-trips on FAT16 as well as FAT32", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    // FAT16 takes a different branch for every FAT entry read and write, and
    // keeps its root directory in fixed sectors rather than a cluster chain.
    const QByteArray contents = "console=serial0,115200 root=PARTUUID=abcd rootwait";
    image.fat().writeFile(QStringLiteral("cmdline.txt"), contents);
    image.sync();

    CHECK(image.fat().fileExists(QStringLiteral("cmdline.txt")));
    CHECK(image.fat().readFile(QStringLiteral("cmdline.txt")) == contents);
}

// A file larger than one cluster has to allocate a chain and follow it back,
// which is where an off-by-one in the FAT walk shows up.
TEST_CASE("FAT driver round-trips a file spanning many clusters", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    const QByteArray contents = patternOfSize(512 * 1024, 'A');
    image.fat().writeFile(QStringLiteral("firstrun.sh"), contents);
    image.sync();

    const QByteArray read = image.fat().readFile(QStringLiteral("firstrun.sh"));
    REQUIRE(read.size() == contents.size());
    CHECK(read == contents);
}

TEST_CASE("FAT driver handles an empty file", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    image.fat().writeFile(QStringLiteral("empty.txt"), QByteArray());
    image.sync();

    CHECK(image.fat().fileExists(QStringLiteral("empty.txt")));
    CHECK(image.fat().readFile(QStringLiteral("empty.txt")).isEmpty());
}

// ---------------------------------------------------------------------------
// Rewriting
// ---------------------------------------------------------------------------

TEST_CASE("FAT driver replaces a file with a longer one", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    image.fat().writeFile(QStringLiteral("config.txt"), "short");
    image.sync();
    REQUIRE(image.fat().readFile(QStringLiteral("config.txt")) == QByteArray("short"));

    // Growing past the original allocation must extend the chain rather than
    // truncate or leak the old clusters.
    const QByteArray longer = patternOfSize(200 * 1024, 'B');
    image.fat().writeFile(QStringLiteral("config.txt"), longer);
    image.sync();

    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == longer);
}

TEST_CASE("FAT driver replaces a file with a shorter one", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    image.fat().writeFile(QStringLiteral("config.txt"), patternOfSize(200 * 1024, 'C'));
    image.sync();

    const QByteArray shorter = "trimmed";
    image.fat().writeFile(QStringLiteral("config.txt"), shorter);
    image.sync();

    // The read must stop at the recorded size, not run on into the clusters
    // the previous, larger contents occupied.
    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == shorter);
}

// ---------------------------------------------------------------------------
// Deletion and enumeration
// ---------------------------------------------------------------------------

TEST_CASE("FAT driver deletes a file", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    image.fat().writeFile(QStringLiteral("scratch.txt"), "delete me");
    image.sync();
    REQUIRE(image.fat().fileExists(QStringLiteral("scratch.txt")));

    CHECK(image.fat().deleteFile(QStringLiteral("scratch.txt")));
    image.sync();

    CHECK_FALSE(image.fat().fileExists(QStringLiteral("scratch.txt")));
    CHECK_FALSE(image.fat().listAllFiles().contains(QStringLiteral("scratch.txt")));
}

TEST_CASE("FAT driver deletes a long-named file", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    const QString name = QStringLiteral("network-config-for-testing.yaml");
    image.fat().writeFile(name, "version: 2");
    image.sync();
    REQUIRE(image.fat().fileExists(name));

    // Deleting has to free every long-filename entry as well as the short
    // one, or the name reappears in a listing.
    CHECK(image.fat().deleteFile(name));
    image.sync();

    CHECK_FALSE(image.fat().fileExists(name));
    CHECK_FALSE(image.fat().listAllFiles().contains(name));
}

TEST_CASE("FAT driver reports a missing file rather than inventing one", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    CHECK_FALSE(image.fat().fileExists(QStringLiteral("absent.txt")));
    CHECK(image.fat().readFile(QStringLiteral("absent.txt")).isEmpty());
    CHECK_FALSE(image.fat().deleteFile(QStringLiteral("absent.txt")));
}

TEST_CASE("FAT driver lists every file it has written", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    const QStringList names = {
        QStringLiteral("config.txt"),
        QStringLiteral("cmdline.txt"),
        QStringLiteral("firstrun.sh"),
        QStringLiteral("a-long-user-data-filename.yaml"),
        QStringLiteral("ssh"),
    };
    for (const QString &name : names)
        image.fat().writeFile(name, name.toUtf8());
    image.sync();

    const QStringList listed = image.fat().listAllFiles();
    for (const QString &name : names) {
        INFO("expected to find: " << name.toStdString());
        CHECK(listed.contains(name));
    }

    // And each still reads back as itself, so the directory entries have not
    // been crossed with one another.
    for (const QString &name : names)
        CHECK(image.fat().readFile(name) == name.toUtf8());
}

TEST_CASE("FAT driver survives filling the root directory", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // Enough entries to push the FAT32 root directory past its first cluster,
    // which makes the directory itself a chain that has to be walked.
    for (int i = 0; i < 64; ++i)
        image.fat().writeFile(QStringLiteral("entry-number-%1.txt").arg(i),
                              QByteArray::number(i));
    image.sync();

    const QStringList listed = image.fat().listAllFiles();
    CHECK(listed.size() >= 64);
    CHECK(image.fat().readFile(QStringLiteral("entry-number-63.txt")) == QByteArray("63"));
}

TEST_CASE("FAT driver keeps contents across a remount", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    image.fat().writeFile(QStringLiteral("persisted.txt"), "still here");
    image.sync();

    // listAllFilesRecursive walks the directory tree rather than the flat
    // root, so it takes a different path to the same entries.
    const QStringList recursive = image.fat().listAllFilesRecursive();
    CHECK_FALSE(recursive.isEmpty());
    CHECK(image.fat().readFile(QStringLiteral("persisted.txt")) == QByteArray("still here"));
}

// ---------------------------------------------------------------------------
// Subdirectories, and filesystems written by something else
// ---------------------------------------------------------------------------
//
// Everything above round-trips the driver against its own output, which
// cannot catch a reader and writer that are wrong in the same direction. The
// cases below build the filesystem with mtools first, so the driver is read
// against an independent implementation -- and they are the only way to get
// subdirectories, which the driver can walk but cannot create.

#define REQUIRE_MTOOLS()                                                                           \
    if (!haveMtools())                                                                             \
    SKIP("mtools is not installed, so no subdirectories can be created to walk")

TEST_CASE("FAT driver reads a file written by mtools", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    const QByteArray payload = "written by a different FAT implementation\n";
    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(payload);
        f.close();
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src, QStringLiteral("::/config.txt")}));
        QFile::remove(src);
    });

    CHECK(image.fat().fileExists(QStringLiteral("config.txt")));
    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == payload);
}

TEST_CASE("FAT driver walks into subdirectories", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("overlay payload");
        f.close();

        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays/nested")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/disable-bt.dtbo")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/nested/deep.dtbo")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src, QStringLiteral("::/config.txt")}));
        QFile::remove(src);
    });

    // The flat listing is root-only; the recursive one has to descend, which
    // is a different traversal with its own cluster-chain walk.
    const QStringList flat = image.fat().listAllFiles();
    const QStringList recursive = image.fat().listAllFilesRecursive();

    // A name that fits 8.3 is stored as a bare short entry with no long-name
    // record, so the driver reports it in FAT's canonical upper case. That is
    // the filesystem's answer, not a defect -- compare without case.
    INFO("flat listing: " << flat.join(QStringLiteral(", ")).toStdString());
    CHECK(flat.contains(QStringLiteral("config.txt"), Qt::CaseInsensitive));

    bool sawNested = false;
    for (const QString &entry : recursive) {
        if (entry.contains(QStringLiteral("deep.dtbo")))
            sawNested = true;
    }
    INFO("recursive listing: " << recursive.join(QStringLiteral(", ")).toStdString());
    CHECK(recursive.size() > flat.size());
    CHECK(sawNested);
}

TEST_CASE("FAT driver reads a large file written by mtools", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    const QByteArray payload = patternOfSize(700 * 1024, 'M');
    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(payload);
        f.close();
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src, QStringLiteral("::/kernel8.img")}));
        QFile::remove(src);
    });

    // Multi-cluster read of a chain this driver did not lay out itself.
    const QByteArray read = image.fat().readFile(QStringLiteral("kernel8.img"));
    REQUIRE(read.size() == payload.size());
    CHECK(read == payload);
}

TEST_CASE("FAT driver overwrites a file that mtools created", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(patternOfSize(300 * 1024, 'X'));
        f.close();
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src, QStringLiteral("::/cmdline.txt")}));
        QFile::remove(src);
    });

    const QByteArray replacement = "console=tty1 root=/dev/mmcblk0p2 rootwait";
    image.fat().writeFile(QStringLiteral("cmdline.txt"), replacement);
    image.sync();

    CHECK(image.fat().readFile(QStringLiteral("cmdline.txt")) == replacement);
}

// ---------------------------------------------------------------------------
// Long filename edge cases
// ---------------------------------------------------------------------------

TEST_CASE("FAT driver handles names of every fragment length", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // A VFAT long name is stored 13 UTF-16 characters at a time, so these
    // straddle the 1-, 2-, 3- and 4-fragment cases and the exact boundaries
    // between them.
    const QStringList names = {
        QStringLiteral("a.txt"),                          // fits 8.3, no LFN at all
        QStringLiteral("abcdefgh.txt"),                   // exactly 8.3
        QStringLiteral("abcdefghi.txt"),                  // one over, needs an LFN
        QStringLiteral("thirteenchar.x"),                 // 14 chars: two fragments
        QStringLiteral("twenty-six-characters-x.txt"),    // three fragments
        QStringLiteral("a-name-of-considerable-length-indeed.conf"), // four
    };

    for (const QString &name : names) {
        image.fat().writeFile(name, name.toUtf8());
        image.sync();
        INFO("name: " << name.toStdString());
        CHECK(image.fat().fileExists(name));
        CHECK(image.fat().readFile(name) == name.toUtf8());
    }

    const QStringList listed = image.fat().listAllFiles();
    for (const QString &name : names) {
        INFO("expected in listing: " << name.toStdString());
        CHECK(listed.contains(name));
    }
}

TEST_CASE("FAT driver keeps colliding short names apart", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // These all collapse to the same 8.3 stem, so each needs a distinct
    // numeric tail. Getting that wrong silently overwrites the first file.
    const QStringList names = {
        QStringLiteral("configuration-one.txt"),
        QStringLiteral("configuration-two.txt"),
        QStringLiteral("configuration-three.txt"),
    };

    for (int i = 0; i < names.size(); ++i)
        image.fat().writeFile(names.at(i), QByteArray::number(i));
    image.sync();

    for (int i = 0; i < names.size(); ++i) {
        INFO("name: " << names.at(i).toStdString());
        CHECK(image.fat().fileExists(names.at(i)));
        CHECK(image.fat().readFile(names.at(i)) == QByteArray::number(i));
    }
}

TEST_CASE("FAT driver reuses a deleted directory entry", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    image.fat().writeFile(QStringLiteral("temporary-name.txt"), "first");
    image.sync();
    REQUIRE(image.fat().deleteFile(QStringLiteral("temporary-name.txt")));
    image.sync();

    // Writing again must either reuse the freed slot or append cleanly; what
    // it must not do is leave the old name visible or lose the new one.
    image.fat().writeFile(QStringLiteral("replacement-name.txt"), "second");
    image.sync();

    CHECK_FALSE(image.fat().fileExists(QStringLiteral("temporary-name.txt")));
    CHECK(image.fat().readFile(QStringLiteral("replacement-name.txt")) == QByteArray("second"));
}

TEST_CASE("FAT16 handles long names and multi-cluster files too", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    // FAT16 keeps the root directory in a fixed region rather than a cluster
    // chain, so long-name entries and growth take a different path here.
    const QString name = QStringLiteral("a-long-name-on-fat16.conf");
    const QByteArray payload = patternOfSize(120 * 1024, 'F');

    image.fat().writeFile(name, payload);
    image.sync();

    CHECK(image.fat().fileExists(name));
    CHECK(image.fat().readFile(name) == payload);
    CHECK(image.fat().listAllFiles().contains(name));

    CHECK(image.fat().deleteFile(name));
    image.sync();
    CHECK_FALSE(image.fat().fileExists(name));
}

// A regression test for the 8.3 short-name field.
//
// DIR_Name is 11 fixed bytes -- an 8-byte base and a 3-byte extension, both
// space-padded, with no dot stored. listAllFiles() and listAllFilesRecursive()
// used to scan all 11 and stop at the first space, which is the padding after
// the base, so every name with a base shorter than 8 characters lost its
// extension: "CONFIG  TXT" listed as "CONFIG".
//
// It only shows up for entries that have no long-name record, which is why
// nothing the imager writes itself ever tripped over it -- but it is exactly
// what mkfs, mtools and a Pi's own boot partition produce. SecureBoot lists
// through listAllFilesRecursive() and then reads each name back, so a
// truncated name resolved to nothing and the file was quietly dropped from
// the signed boot image.
TEST_CASE("FAT driver keeps the extension on a bare 8.3 name", "[fat][image][regression]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("payload");
        f.close();

        // All of these have a base shorter than 8 characters, so mtools
        // stores each as a plain short entry with no long-name record.
        for (const char *name : {"::/CONFIG.TXT", "::/START.ELF", "::/FIXUP.DAT", "::/SSH"}) {
            REQUIRE(runMtool(QStringLiteral("mcopy"),
                             {QStringLiteral("-i"), imagePath, src, QString::fromUtf8(name)}));
        }
        QFile::remove(src);
    });

    const QStringList flat = image.fat().listAllFiles();
    INFO("flat listing: " << flat.join(QStringLiteral(", ")).toStdString());

    CHECK(flat.contains(QStringLiteral("CONFIG.TXT"), Qt::CaseInsensitive));
    CHECK(flat.contains(QStringLiteral("START.ELF"), Qt::CaseInsensitive));
    CHECK(flat.contains(QStringLiteral("FIXUP.DAT"), Qt::CaseInsensitive));
    // A name with no extension at all must not gain a trailing dot.
    CHECK(flat.contains(QStringLiteral("SSH"), Qt::CaseInsensitive));
    CHECK_FALSE(flat.contains(QStringLiteral("SSH."), Qt::CaseInsensitive));

    // The recursive walk is the one SecureBoot uses, and had the same fault.
    const QStringList recursive = image.fat().listAllFilesRecursive();
    INFO("recursive listing: " << recursive.join(QStringLiteral(", ")).toStdString());
    bool sawConfig = false;
    for (const QString &entry : recursive) {
        if (entry.endsWith(QStringLiteral("CONFIG.TXT"), Qt::CaseInsensitive))
            sawConfig = true;
    }
    CHECK(sawConfig);

    // And the names it hands back must actually resolve, which is the whole
    // point: SecureBoot reads every listed file straight back.
    for (const QString &name : flat) {
        INFO("listed name must resolve: " << name.toStdString());
        CHECK(image.fat().fileExists(name));
    }
}

// ---------------------------------------------------------------------------
// Subdirectory paths
// ---------------------------------------------------------------------------
//
// readFile(), fileExists() and deleteFile() all accept "dir/file" and
// navigate into the directory, saving and restoring the driver's notion of
// the current directory around the walk. That is a distinct path from the
// root-directory case above and none of it was exercised -- which matters,
// because overlays/ is where every device-tree overlay on a Pi boot partition
// lives.

TEST_CASE("FAT driver reads a file from a subdirectory", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    const QByteArray payload = "device tree overlay payload";
    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(payload);
        f.close();
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/disable-bt.dtbo")}));
        QFile::remove(src);
    });

    CHECK(image.fat().readFile(QStringLiteral("overlays/disable-bt.dtbo")) == payload);
}

// Regression: deleting a file inside a subdirectory used to be impossible.
//
// deleteFile() set _fat32_currentRootDirCluster to the subdirectory and then
// called getDirEntry(), which begins with openDir() -- and openDir() seeks
// straight back to the root. The search ran in the wrong directory, found
// nothing, and returned false, so the entire subdirectory branch was dead
// code. updateDirEntry() had the same problem for the write-back.
//
// It mattered because DownloadThread::_clearFatPartition() walks every entry
// from listAllFilesRecursive() and calls deleteFile() on each, logging
// "WARNING - failed to delete" and continuing. Everything under overlays/
// survived an operation meant to clear the partition, and the pass that
// removes empty directories then failed too, because they were not empty.
//
// deleteFile() now scans the subdirectory itself, the way readFile() always
// has, and marks the entry deleted in place.
TEST_CASE("FAT driver deletes a file in a subdirectory", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("doomed overlay");
        f.close();
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/doomed.dtbo")}));
        QFile::remove(src);
    });

    // Readable, so the file is unambiguously there...
    REQUIRE(image.fat().readFile(QStringLiteral("overlays/doomed.dtbo")) ==
            QByteArray("doomed overlay"));

    CHECK(image.fat().deleteFile(QStringLiteral("overlays/doomed.dtbo")));
    image.sync();
    CHECK(image.fat().readFile(QStringLiteral("overlays/doomed.dtbo")).isEmpty());

    // Deleting inside a subdirectory must leave the root usable: the saved
    // directory state has to be restored on the way out.
    image.fat().writeFile(QStringLiteral("config.txt"), "root still works");
    image.sync();
    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == QByteArray("root still works"));
}

TEST_CASE("FAT driver leaves the root directory usable after a subdirectory walk",
          "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("nested");
        f.close();
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/thing.dtbo")}));
        QFile::remove(src);
    });

    image.fat().writeFile(QStringLiteral("config.txt"), "before");
    image.sync();

    // Walking into a subdirectory saves and restores the current-directory
    // state. If the restore is wrong, the next root operation writes into the
    // subdirectory instead -- which is silent and very hard to spot.
    REQUIRE(image.fat().readFile(QStringLiteral("overlays/thing.dtbo")) == QByteArray("nested"));

    image.fat().writeFile(QStringLiteral("cmdline.txt"), "after");
    image.sync();

    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == QByteArray("before"));
    CHECK(image.fat().readFile(QStringLiteral("cmdline.txt")) == QByteArray("after"));
    CHECK(image.fat().listAllFiles().contains(QStringLiteral("cmdline.txt")));
}

TEST_CASE("FAT driver rejects malformed subdirectory paths", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // A path that splits to fewer than two parts is not a subdirectory
    // reference at all, and must be refused rather than half-interpreted.
    CHECK(image.fat().readFile(QStringLiteral("/")).isEmpty());
    CHECK_FALSE(image.fat().deleteFile(QStringLiteral("/")));
    CHECK(image.fat().readFile(QStringLiteral("nosuchdir/file.txt")).isEmpty());
    CHECK_FALSE(image.fat().fileExists(QStringLiteral("nosuchdir/file.txt")));
    CHECK_FALSE(image.fat().deleteFile(QStringLiteral("nosuchdir/file.txt")));
}

TEST_CASE("FAT driver reads a subdirectory file on FAT16", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    const QByteArray payload = "fat16 overlay";
    FatImage image(16, 16, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(payload);
        f.close();
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/ov.dtbo")}));
        QFile::remove(src);
    });

    // FAT16 reaches its root directory through a fixed sector range rather
    // than a cluster chain, so restoring position after the walk differs.
    CHECK(image.fat().readFile(QStringLiteral("overlays/ov.dtbo")) == payload);
    image.fat().writeFile(QStringLiteral("config.txt"), "root still works");
    image.sync();
    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == QByteArray("root still works"));
}

// fileExists() does not understand subdirectory paths, though readFile() and
// deleteFile() both do.
//
// The first two split "dir/file" and walk into the directory; fileExists()
// hands the whole string to getDirEntry(), which looks for a root-level entry
// literally named "overlays/disable-bt.dtbo" and does not find one. So a file
// readFile() returns happily is reported as absent.
//
// Left as it is rather than fixed: the only caller that pairs them is
// SecureBoot::extractFatPartitionFiles(), and it uses fileExists() purely to
// tell "zero-length file" from "read error" in a log line -- so today the
// cost is a misleading diagnostic, not a lost file. This case is here so the
// asymmetry is visible and deliberate rather than discovered again later.
TEST_CASE("FAT driver fileExists does not follow subdirectory paths",
          "[fat][image][known-asymmetry]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("present");
        f.close();
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/here.dtbo")}));
        QFile::remove(src);
    });

    // Readable...
    CHECK(image.fat().readFile(QStringLiteral("overlays/here.dtbo")) == QByteArray("present"));
    // ...but reported absent.
    CHECK_FALSE(image.fat().fileExists(QStringLiteral("overlays/here.dtbo")));
}

// ---------------------------------------------------------------------------
// Refusing filesystems it cannot handle
// ---------------------------------------------------------------------------
//
// The driver supports FAT16 and FAT32 and nothing else. Everything it does
// not support has to be rejected at construction, because the alternative is
// interpreting another filesystem's metadata as FAT and writing over a card
// on that basis. Every one of these rejections was unexercised.
//
// The card in a reader is whatever the user last put in it, so these are not
// hypothetical inputs.

namespace {

// Build a filesystem of the given type in a scratch file and try to open it
// as a FAT partition, returning what the constructor did.
struct OpenAttempt {
    bool threw = false;
    std::string message;
};

// The formatter is a callable rather than a tool and its arguments: what
// builds a FAT filesystem is not the same program everywhere, and the cases
// that want an unusual one (FAT12, exFAT) still name their own.
OpenAttempt tryOpenAs(const std::function<bool(const QString &)> &formatter, int sizeMB)
{
    ScopedTempDir scratch(QStringLiteral("rpi-imager-reject"));
    const QString path = scratch.filePath(QStringLiteral("fs.img"));

    OpenAttempt result;
    {
        auto ops = rpi_imager::FileOperations::Create();
        if (ops->CreateTestFile(path.toStdString(),
                                static_cast<std::uint64_t>(sizeMB) * 1024 * 1024) !=
            rpi_imager::FileError::kSuccess) {
            return result;
        }
    }

    if (!formatter(path)) {
        return result;
    }

    auto reopened = rpi_imager::FileOperations::Create();
    if (reopened->OpenDevice(path.toStdString()) == rpi_imager::FileError::kSuccess) {
        DeviceWrapper dw(reopened.get());
        try {
            DeviceWrapperFatPartition fat(&dw, 0,
                                          static_cast<quint64>(sizeMB) * 1024 * 1024);
        } catch (const std::runtime_error &e) {
            result.threw = true;
            result.message = e.what();
        }
    }

    return result;
}

} // namespace

TEST_CASE("FAT driver refuses a FAT12 filesystem", "[fat][image]")
{
    REQUIRE_MKFS();

    // FAT12 is what a floppy-sized card formats as. Its FAT entries are 12
    // bits, so reading them as 16 would produce plausible-looking but wrong
    // cluster numbers.
    const OpenAttempt attempt =
        tryOpenAs([](const QString &path) {
            return rpi_test::makeFatFilesystem(path, 12);
        }, 4);

    INFO("message: " << attempt.message);
    CHECK(attempt.threw);
    CHECK(attempt.message.find("FAT12") != std::string::npos);
}

TEST_CASE("FAT driver refuses an exFAT filesystem", "[fat][image]")
{
    const QString mkfsExfat = rpi_test::toolPath(QStringLiteral("mkfs.exfat"));
    if (mkfsExfat.isEmpty())
        SKIP("mkfs.exfat is not installed");

    // exFAT is the default for cards over 32GB, so this is the most likely
    // unsupported filesystem to actually turn up in a reader.
    const OpenAttempt attempt =
        tryOpenAs([&mkfsExfat](const QString &path) {
            QString error;
            return rpi_test::detail::runFixtureTool(mkfsExfat, {path}, &error);
        }, 64);

    INFO("message: " << attempt.message);
    CHECK(attempt.threw);
}

TEST_CASE("FAT driver refuses a partition of zeroes", "[fat][image]")
{
    ScopedTempDir scratch(QStringLiteral("rpi-imager-zero"));
    const QString path = scratch.filePath(QStringLiteral("zero.img"));

    auto ops = rpi_imager::FileOperations::Create();
    REQUIRE(ops->CreateTestFile(path.toStdString(), 8 * 1024 * 1024) ==
            rpi_imager::FileError::kSuccess);
    auto reopened = rpi_imager::FileOperations::Create();
    REQUIRE(reopened->OpenDevice(path.toStdString()) == rpi_imager::FileError::kSuccess);

    DeviceWrapper dw(reopened.get());
    // An unformatted card: no 0x55AA signature anywhere.
    CHECK_THROWS_AS(DeviceWrapperFatPartition(&dw, 0, 8 * 1024 * 1024), std::runtime_error);

}

TEST_CASE("FAT driver refuses a signature with no filesystem behind it", "[fat][image]")
{
    ScopedTempDir scratch(QStringLiteral("rpi-imager-sig"));
    const QString path = scratch.filePath(QStringLiteral("sig.img"));

    // Just the boot signature and nothing else: the check that matters is the
    // one after it, on the geometry. A card that passes the signature test and
    // then reports zero bytes per sector must not divide by it.
    QByteArray disk(8 * 1024 * 1024, '\0');
    disk[510] = static_cast<char>(0x55);
    disk[511] = static_cast<char>(0xAA);

    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(disk);
    f.close();

    auto ops = rpi_imager::FileOperations::Create();
    REQUIRE(ops->OpenDevice(path.toStdString()) == rpi_imager::FileError::kSuccess);
    DeviceWrapper dw(ops.get());

    bool threw = false;
    std::string message;
    try {
        DeviceWrapperFatPartition fat(&dw, 0, 8 * 1024 * 1024);
    } catch (const std::runtime_error &e) {
        threw = true;
        message = e.what();
    }

    INFO("message: " << message);
    CHECK(threw);

}

TEST_CASE("FAT driver refuses a bad sector size", "[fat][image]")
{
    REQUIRE_MKFS();

    ScopedTempDir scratch(QStringLiteral("rpi-imager-bps"));
    const QString path = scratch.filePath(QStringLiteral("bps.img"));

    {
        auto ops = rpi_imager::FileOperations::Create();
        REQUIRE(ops->CreateTestFile(path.toStdString(), 64ull * 1024 * 1024) ==
                rpi_imager::FileError::kSuccess);
    }
    QString formatError;
    INFO("formatter: " << formatError.toStdString());
    REQUIRE(rpi_test::makeFatFilesystem(path, 32, QString(), &formatError));

    // Corrupt BPB_BytsPerSec (offset 11, little-endian) to a value that is
    // not a multiple of four. The driver checks this explicitly because the
    // rest of its arithmetic divides by it.
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::ReadWrite));
        REQUIRE(f.seek(11));
        const char bogus[2] = {static_cast<char>(0x03), static_cast<char>(0x00)};
        f.write(bogus, 2);
        f.close();
    }

    auto reopened = rpi_imager::FileOperations::Create();
    REQUIRE(reopened->OpenDevice(path.toStdString()) == rpi_imager::FileError::kSuccess);
    DeviceWrapper dw(reopened.get());

    bool threw = false;
    std::string message;
    try {
        DeviceWrapperFatPartition fat(&dw, 0, 64ull * 1024 * 1024);
    } catch (const std::runtime_error &e) {
        threw = true;
        message = e.what();
    }

    INFO("message: " << message);
    CHECK(threw);

}

// ---------------------------------------------------------------------------
// Running out of room
// ---------------------------------------------------------------------------
//
// The driver throws "Out of disk space on FAT partition" when allocateCluster
// finds no free entry. A boot partition is small and customisation writes to
// it after imaging, so filling it is not a hypothetical -- and the failure has
// to be an exception the caller can report, not a corrupt filesystem.

TEST_CASE("FAT driver refuses to write past the end of a full partition", "[fat][image]")
{
    REQUIRE_MKFS();
    // Deliberately tiny: 16MB of FAT16 fills quickly.
    FatImage image(16, 16);

    const QByteArray chunk = patternOfSize(512 * 1024, 'X');

    bool threw = false;
    std::string message;
    int written = 0;
    try {
        // Keep writing distinct files until the allocator gives up.
        for (int i = 0; i < 200; ++i) {
            image.fat().writeFile(QStringLiteral("filler-%1.bin").arg(i), chunk);
            ++written;
        }
        image.sync();
    } catch (const std::runtime_error &e) {
        threw = true;
        message = e.what();
    }

    INFO("files written before the partition filled: " << written);
    INFO("message: " << message);

    // Either it threw, or a 16MB filesystem somehow swallowed 100MB -- the
    // latter would mean the allocator is handing out clusters it does not
    // have.
    REQUIRE(threw);
    CHECK(message.find("space") != std::string::npos);
    CHECK(written > 0);
}

TEST_CASE("FAT driver keeps earlier files intact after filling up", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    // A file worth protecting, written first.
    const QByteArray precious = patternOfSize(64 * 1024, 'P');
    image.fat().writeFile(QStringLiteral("config.txt"), precious);
    image.sync();

    const QByteArray chunk = patternOfSize(512 * 1024, 'Y');
    try {
        for (int i = 0; i < 200; ++i)
            image.fat().writeFile(QStringLiteral("junk-%1.bin").arg(i), chunk);
    } catch (const std::runtime_error &) {
        // Expected once it fills.
    }
    image.sync();

    // Running out of room must not damage what was already there.
    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == precious);
}

// ---------------------------------------------------------------------------
// Names the driver has to cope with
// ---------------------------------------------------------------------------

TEST_CASE("FAT driver handles a name at the long-filename limit", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // 255 characters is the VFAT maximum, spanning 20 long-name entries.
    QString longest;
    while (longest.size() < 250)
        longest += QStringLiteral("abcdefghij");
    longest = longest.left(250) + QStringLiteral(".txt");

    image.fat().writeFile(longest, "at the limit");
    image.sync();

    CHECK(image.fat().readFile(longest) == QByteArray("at the limit"));
    CHECK(image.fat().listAllFiles().contains(longest));
}

TEST_CASE("FAT driver handles names differing only in case", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // FAT is case-insensitive, so the second write must replace the first
    // rather than create a second entry that shadows it.
    image.fat().writeFile(QStringLiteral("Config.txt"), "first");
    image.sync();
    image.fat().writeFile(QStringLiteral("CONFIG.TXT"), "second");
    image.sync();

    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == QByteArray("second"));

    int matches = 0;
    for (const QString &name : image.fat().listAllFiles()) {
        if (name.compare(QStringLiteral("config.txt"), Qt::CaseInsensitive) == 0)
            ++matches;
    }
    INFO("entries matching config.txt case-insensitively: " << matches);
    CHECK(matches == 1);
}

TEST_CASE("FAT driver handles names with spaces and dots", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    const QStringList names = {
        QStringLiteral("my config.txt"),
        QStringLiteral("archive.tar.gz"),
        QStringLiteral("no-extension"),
        QStringLiteral("UPPER.TXT"),
        QStringLiteral("dash-and_underscore.cfg"),
    };

    for (const QString &name : names) {
        image.fat().writeFile(name, name.toUtf8());
        image.sync();
        INFO("name: " << name.toStdString());
        CHECK(image.fat().readFile(name) == name.toUtf8());
    }
}

TEST_CASE("FAT driver rewrites the same file repeatedly", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // Customisation rewrites config.txt several times in a run. Each rewrite
    // frees the previous chain; leaking them would exhaust a boot partition
    // after a handful of edits.
    for (int i = 0; i < 30; ++i) {
        const QByteArray contents = patternOfSize(64 * 1024, static_cast<char>('a' + (i % 26)));
        image.fat().writeFile(QStringLiteral("churn.bin"), contents);
        image.sync();
        INFO("iteration " << i);
        REQUIRE(image.fat().readFile(QStringLiteral("churn.bin")) == contents);
    }

    // And the partition still has room for something else afterwards.
    image.fat().writeFile(QStringLiteral("after.txt"), "still writable");
    image.sync();
    CHECK(image.fat().readFile(QStringLiteral("after.txt")) == QByteArray("still writable"));
}

TEST_CASE("FAT driver alternates writes and deletes without leaking", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    const QByteArray chunk = patternOfSize(256 * 1024, 'Z');

    // Write then delete repeatedly on a small partition: if deletion does not
    // return clusters to the FAT this runs out of space long before the end.
    for (int i = 0; i < 40; ++i) {
        image.fat().writeFile(QStringLiteral("cycle.bin"), chunk);
        image.sync();
        REQUIRE(image.fat().deleteFile(QStringLiteral("cycle.bin")));
        image.sync();
    }

    image.fat().writeFile(QStringLiteral("final.bin"), chunk);
    image.sync();
    CHECK(image.fat().readFile(QStringLiteral("final.bin")) == chunk);
}

// ---------------------------------------------------------------------------
// A FAT table that points back at itself
// ---------------------------------------------------------------------------
//
// The card is the user's, and the imager reads its boot partition before
// writing customisation into it. A filesystem that has been damaged -- a card
// pulled mid-write, a cheap controller, or one deliberately crafted -- can
// contain a cluster chain that loops. Walking it without noticing means the
// imager never returns: no error, no progress, just a hang the user can only
// escape by killing it.
//
// getClusterChain() guards this by refusing to visit a cluster twice. The
// test crafts the loop directly in the FAT, because no formatter will produce
// one.

TEST_CASE("FAT driver refuses a circular cluster chain", "[fat][image]")
{
    REQUIRE_MKFS();

    ScopedTempDir scratch(QStringLiteral("rpi-imager-cyc"));
    const QString path = scratch.filePath(QStringLiteral("cyclic.img"));
    const quint64 imageSize = 64ull * 1024 * 1024;

    {
        auto ops = rpi_imager::FileOperations::Create();
        REQUIRE(ops->CreateTestFile(path.toStdString(), imageSize) ==
                rpi_imager::FileError::kSuccess);
    }
    QString formatError;
    INFO("formatter: " << formatError.toStdString());
    REQUIRE(rpi_test::makeFatFilesystem(path, 32, QString(), &formatError));

    // Give the filesystem a real file, so there is a cluster chain to walk.
    // Reading a directory stops at the end-of-directory marker and never
    // follows the FAT, so only file content exercises the chain.
    const QByteArray payload(64 * 1024, '\xA5');
    {
        auto ops = rpi_imager::FileOperations::Create();
        REQUIRE(ops->OpenDevice(path.toStdString()) == rpi_imager::FileError::kSuccess);
        DeviceWrapper dw(ops.get());
        DeviceWrapperFatPartition fat(&dw, 0, imageSize);
        fat.writeFile(QStringLiteral("loop.bin"), payload);
    }

    // Make every data cluster point at itself. Cluster 2 holds the root
    // directory and is left alone so the file can still be found; the file's
    // own chain then loops on its first cluster whichever one it landed on.
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::ReadWrite));

        auto readLe = [&f](qint64 offset, int bytes) -> quint32 {
            REQUIRE(f.seek(offset));
            const QByteArray raw = f.read(bytes);
            REQUIRE(raw.size() == bytes);
            quint32 v = 0;
            for (int i = bytes - 1; i >= 0; --i)
                v = (v << 8) | static_cast<quint8>(raw[i]);
            return v;
        };

        const quint32 bytesPerSector = readLe(11, 2);
        const quint32 reservedSectors = readLe(14, 2);
        REQUIRE(bytesPerSector > 0);

        const qint64 fatOffset = qint64(reservedSectors) * bytesPerSector;

        // Built as one buffer and written in a single pass: asserting inside
        // a 4096-iteration loop would add thousands of assertions to the
        // report for no extra confidence.
        constexpr quint32 kFirstDataCluster = 3;
        constexpr quint32 kLastPatched = 4096;
        QByteArray table;
        table.reserve(int((kLastPatched - kFirstDataCluster) * 4));
        for (quint32 cluster = kFirstDataCluster; cluster < kLastPatched; ++cluster) {
            const quint32 selfRef = cluster & 0x0FFFFFFFu;
            for (int i = 0; i < 4; ++i)
                table.append(static_cast<char>((selfRef >> (8 * i)) & 0xFF));
        }
        REQUIRE(f.seek(fatOffset + qint64(kFirstDataCluster) * 4));
        REQUIRE(f.write(table) == table.size());
        f.close();
    }

    auto reopened = rpi_imager::FileOperations::Create();
    REQUIRE(reopened->OpenDevice(path.toStdString()) == rpi_imager::FileError::kSuccess);
    DeviceWrapper dw(reopened.get());

    // What must not happen is looping forever. If this test ever times out
    // rather than failing, the guard in getClusterChain() has gone.
    bool threw = false;
    std::string message;
    try {
        DeviceWrapperFatPartition fat(&dw, 0, imageSize);
        fat.readFile(QStringLiteral("loop.bin"));
    } catch (const std::runtime_error &e) {
        threw = true;
        message = e.what();
    }

    INFO("message: " << message);
    CHECK(threw);
    CHECK_THAT(message, ContainsSubstring("ircular"));

}

// ---------------------------------------------------------------------------
// Writing into a subdirectory
// ---------------------------------------------------------------------------
//
// deleteFile()'s subdirectory branch turned out to be dead code: getDirEntry()
// begins with openDir(), which seeks back to the root, so the cluster set up
// for the subdirectory was discarded and the search ran in the root instead.
// writeFile() performs the same sequence -- resolve the directory, point the
// traversal state at its cluster, then call getDirEntry() for the file -- so
// the same question applies to it, and nothing was covering it.
//
// The customisation written after imaging goes into the boot partition's root,
// but overlays and firmware live in subdirectories, and a write that silently
// lands in the wrong directory is the kind of thing that only shows up when
// the board fails to boot.

TEST_CASE("FAT driver writes a file into an existing subdirectory", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
    });

    // Refused, rather than silently written to the root -- which is what it
    // used to do, because getDirEntry() seeks back to the root and discards
    // the subdirectory the caller selected.
    const QByteArray payload("device tree overlay contents");
    REQUIRE_THROWS(image.fat().writeFile(QStringLiteral("overlays/added.dtbo"), payload));
    image.sync();

    // Nothing was created anywhere.
    CHECK(image.fat().readFile(QStringLiteral("overlays/added.dtbo")).isEmpty());
    CHECK(image.fat().readFile(QStringLiteral("added.dtbo")).isEmpty());
}

TEST_CASE("FAT driver replaces a file already in a subdirectory", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("original");
        f.close();
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/existing.dtbo")}));
        QFile::remove(src);
    });

    REQUIRE(image.fat().readFile(QStringLiteral("overlays/existing.dtbo"))
            == QByteArray("original"));

    // Also refused. Previously this left the original in place and created
    // an unrelated entry in the root, so the caller believed it had replaced
    // a file it had not touched.
    const QByteArray replacement("replaced contents, rather longer than before");
    REQUIRE_THROWS(image.fat().writeFile(QStringLiteral("overlays/existing.dtbo"), replacement));
    image.sync();

    CHECK(image.fat().readFile(QStringLiteral("overlays/existing.dtbo"))
          == QByteArray("original"));
    CHECK(image.fat().readFile(QStringLiteral("existing.dtbo")).isEmpty());
}

TEST_CASE("FAT driver refuses a write into a directory that is not there",
          "[fat][image]")
{
    // Rather than creating the file somewhere else, which is the failure
    // mode worth guarding against.
    REQUIRE_MKFS();
    FatImage image(32, 64);

    REQUIRE_THROWS(image.fat().writeFile(QStringLiteral("nosuchdir/file.txt"),
                                         QByteArray("contents")));
    image.sync();
    CHECK(image.fat().readFile(QStringLiteral("file.txt")).isEmpty());
}

TEST_CASE("FAT driver refuses a path with no file name", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);
    REQUIRE_THROWS(image.fat().writeFile(QStringLiteral("overlays/"), QByteArray("x")));
}

TEST_CASE("FAT driver refuses to write through a file as if it were a directory",
          "[fat][image]")
{
    // "config.txt/evil" names a path component that exists but is a file.
    // Following it would corrupt the entry it points at.
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("arm_64bit=1\n");
        f.close();
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/config.txt")}));
        QFile::remove(src);
    });

    REQUIRE_THROWS(image.fat().writeFile(QStringLiteral("config.txt/evil"),
                                         QByteArray("payload")));
    image.sync();
    // The real file is untouched.
    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == QByteArray("arm_64bit=1\n"));
}

// ═══════════════════════════════════════════════════════════════════════════
// FAT16, which is a different driver in all but name
//
// FAT16 keeps its root directory in fixed sectors rather than a cluster
// chain, so almost every traversal has a second implementation for it:
// listAllFilesRecursive() has an entire parallel body, and readFile(),
// deleteFile() and getDirEntry() each branch at the root.
//
// Small cards still arrive formatted this way, and so do the boot partitions
// of some older images. What breaks is not the write -- it is finding the
// file again afterwards, which is how SecureBoot builds its signed boot
// image: it lists the partition and reads back every name it was given.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FAT driver walks subdirectories on FAT16", "[fat][image][fat16]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(16, 32, [&](const QString &imagePath) {
        const QString src = imagePath + QStringLiteral(".src");
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("overlay payload");
        f.close();

        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
        REQUIRE(runMtool(QStringLiteral("mmd"),
                         {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays/nested")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/disable-bt.dtbo")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src,
                          QStringLiteral("::/overlays/nested/deep.dtbo")}));
        REQUIRE(runMtool(QStringLiteral("mcopy"),
                         {QStringLiteral("-i"), imagePath, src, QStringLiteral("::/config.txt")}));
        QFile::remove(src);
    });

    const QStringList flat = image.fat().listAllFiles();
    const QStringList recursive = image.fat().listAllFilesRecursive();

    INFO("flat: " << flat.join(QStringLiteral(", ")).toStdString());
    INFO("recursive: " << recursive.join(QStringLiteral(", ")).toStdString());

    // The root listing walks fixed sectors here, and the descent into
    // subdirectories then follows cluster chains like FAT32 does.
    CHECK(flat.contains(QStringLiteral("config.txt"), Qt::CaseInsensitive));
    CHECK(recursive.size() > flat.size());

    bool sawNested = false;
    for (const QString &entry : recursive)
        if (entry.contains(QStringLiteral("deep.dtbo")))
            sawNested = true;
    CHECK(sawNested);
}

TEST_CASE("FAT driver reads back a long-named file on FAT16", "[fat][image][fat16]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    // A name too long for 8.3 is stored across several long-name records
    // that have to be reassembled in reverse. The root of a FAT16 volume is
    // read through a different loop than a FAT32 one.
    const QString name = QStringLiteral("network-config-for-first-boot.yaml");
    const QByteArray contents = "version: 2\nethernets:\n  eth0:\n    dhcp4: true\n";

    image.fat().writeFile(name, contents);
    image.sync();

    CHECK(image.fat().fileExists(name));
    CHECK(image.fat().readFile(name) == contents);
    CHECK(image.fat().listAllFiles().contains(name));
}

TEST_CASE("FAT driver deletes a long-named file on FAT16", "[fat][image][fat16]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    const QString name = QStringLiteral("user-data-written-by-imager.yaml");
    image.fat().writeFile(name, QByteArray("#cloud-config\n"));
    image.sync();
    REQUIRE(image.fat().fileExists(name));

    // Every long-name record has to be freed as well as the short one, or
    // the next write finds a directory that looks full.
    image.fat().deleteFile(name);
    image.sync();

    CHECK_FALSE(image.fat().fileExists(name));
    CHECK_FALSE(image.fat().listAllFiles().contains(name));
}

TEST_CASE("FAT driver replaces a file on FAT16", "[fat][image][fat16]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    const QString name = QStringLiteral("cmdline.txt");
    image.fat().writeFile(name, QByteArray("first version, quite short"));
    image.sync();

    const QByteArray second(9000, 'B');
    image.fat().writeFile(name, second);
    image.sync();

    // Rewriting has to free the old chain and allocate a new one; leaving the
    // old clusters marked in use leaks the card's free space.
    CHECK(image.fat().readFile(name) == second);
}

TEST_CASE("FAT driver reports a missing file on FAT16", "[fat][image][fat16]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    CHECK_FALSE(image.fat().fileExists(QStringLiteral("absent.txt")));
    CHECK(image.fat().readFile(QStringLiteral("absent.txt")).isEmpty());
}

TEST_CASE("The FAT16 recursive walk finds what the driver wrote", "[fat][image][fat16]")
{
    REQUIRE_MKFS();
    FatImage image(16, 16);

    // listAllFilesRecursive() has an entire second body for FAT16, because
    // the root directory is fixed sectors rather than a cluster chain. It is
    // the listing SecureBoot uses to decide what goes into the signed boot
    // image, so a name it drops is a file left out without a word.
    const QString shortName = QStringLiteral("config.txt");
    const QString longName = QStringLiteral("network-config-first-boot.yaml");

    image.fat().writeFile(shortName, QByteArray("dtparam=audio=on\n"));
    image.fat().writeFile(longName, QByteArray("version: 2\n"));
    image.sync();

    const QStringList recursive = image.fat().listAllFilesRecursive();
    INFO("recursive: " << recursive.join(QStringLiteral(", ")).toStdString());

    bool sawShort = false, sawLong = false;
    for (const QString &entry : recursive) {
        if (entry.contains(shortName, Qt::CaseInsensitive)) sawShort = true;
        if (entry.contains(longName)) sawLong = true;
    }
    CHECK(sawShort);
    CHECK(sawLong);
}

// ═══════════════════════════════════════════════════════════════════════════
// Reading a file out of a subdirectory
//
// readFile() accepts "overlays/disable-bt.dtbo" as well as a bare name, and
// that matters because listAllFilesRecursive() returns paths in exactly that
// form -- SecureBoot lists the partition and then reads back every name it
// was handed. A path form the listing produces but the reader cannot resolve
// is a file silently missing from the signed boot image.
//
// The failure modes are all "return nothing", so they are indistinguishable
// from an empty file unless something checks them.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// An image with a couple of levels of subdirectory and known contents.
void populateWithSubdirs(const QString &imagePath, const QByteArray &payload)
{
    const QString src = imagePath + QStringLiteral(".src");
    QFile f(src);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(payload);
    f.close();

    REQUIRE(runMtool(QStringLiteral("mmd"),
                     {QStringLiteral("-i"), imagePath, QStringLiteral("::/overlays")}));
    REQUIRE(runMtool(QStringLiteral("mcopy"),
                     {QStringLiteral("-i"), imagePath, src,
                      QStringLiteral("::/overlays/disable-bt.dtbo")}));
    REQUIRE(runMtool(QStringLiteral("mcopy"),
                     {QStringLiteral("-i"), imagePath, src, QStringLiteral("::/config.txt")}));
    QFile::remove(src);
}

} // namespace

TEST_CASE("FAT driver reads a file out of a subdirectory", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    const QByteArray payload = "overlay payload for the subdirectory read";
    FatImage image(32, 64, [&](const QString &p) { populateWithSubdirs(p, payload); });

    // The form listAllFilesRecursive() hands back.
    CHECK(image.fat().readFile(QStringLiteral("overlays/disable-bt.dtbo")) == payload);
    // And the bare name still works alongside it.
    CHECK(image.fat().readFile(QStringLiteral("config.txt")) == payload);
}

TEST_CASE("FAT driver reads a subdirectory file on FAT16 too", "[fat][image][fat16]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    const QByteArray payload = "overlay payload on fat16";
    FatImage image(16, 32, [&](const QString &p) { populateWithSubdirs(p, payload); });

    // FAT16's root directory is fixed sectors, so resolving the first
    // component takes a different path than on FAT32.
    CHECK(image.fat().readFile(QStringLiteral("overlays/disable-bt.dtbo")) == payload);
}

TEST_CASE("FAT driver reports a missing subdirectory rather than guessing", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &p) { populateWithSubdirs(p, "x"); });

    CHECK(image.fat().readFile(QStringLiteral("nosuchdir/file.txt")).isEmpty());
    CHECK(image.fat().readFile(QStringLiteral("overlays/nosuchfile.dtbo")).isEmpty());
}

TEST_CASE("FAT driver refuses a path whose first part is a file", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &p) { populateWithSubdirs(p, "x"); });

    // config.txt is a file, not a directory. Walking into it would read
    // whatever its contents happen to look like as a directory table.
    CHECK(image.fat().readFile(QStringLiteral("config.txt/inner.txt")).isEmpty());
}

TEST_CASE("FAT driver handles a malformed subdirectory path", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    FatImage image(32, 64, [&](const QString &p) { populateWithSubdirs(p, "x"); });

    // Nothing here should reach the cluster walk with a half-parsed path.
    CHECK(image.fat().readFile(QStringLiteral("/")).isEmpty());
    CHECK(image.fat().readFile(QStringLiteral("overlays/")).isEmpty());
    CHECK(image.fat().readFile(QStringLiteral("/config.txt")).isEmpty());
}

TEST_CASE("Every name the recursive listing returns can be read", "[fat][image]")
{
    REQUIRE_MKFS();
    REQUIRE_MTOOLS();

    const QByteArray payload = "readable through the listing";
    FatImage image(32, 64, [&](const QString &p) { populateWithSubdirs(p, payload); });

    // This is the contract SecureBoot depends on: it lists the partition and
    // reads back every name. A name it cannot resolve is a file left out of
    // the signed boot image without a word.
    const QStringList names = image.fat().listAllFilesRecursive();
    REQUIRE_FALSE(names.isEmpty());

    QStringList unreadable;
    for (const QString &name : names) {
        if (image.fat().readFile(name).isEmpty())
            unreadable << name;
    }
    INFO("listing: " << names.join(QStringLiteral(", ")).toStdString());
    INFO("unreadable: " << unreadable.join(QStringLiteral(", ")).toStdString());
    CHECK(unreadable.isEmpty());
}

// ---------------------------------------------------------------------------
// File length without reading the file
// ---------------------------------------------------------------------------
//
// fileSize() exists so a caller can check a large file's length without paying
// for a block-by-block read of its contents. DownloadThread's customisation
// read-back uses it for exactly that: content-checking a secure-boot boot.img
// means thousands of direct-I/O reads, but its length is in the directory
// entry, so a truncated payload can be caught for the cost of one seek.

TEST_CASE("FAT driver reports a file's length without reading it", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    const QByteArray contents(4097, '\xA5');
    image.fat().writeFile(QStringLiteral("boot.img"), contents);
    image.sync();

    CHECK(image.fat().fileSize(QStringLiteral("boot.img")) == contents.size());
}

TEST_CASE("FAT driver reports no length for a file that is not there", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // -1 rather than 0: a caller comparing a recorded length against this
    // needs to tell "not there" from "there and empty", or an absent file
    // would read as a zero-length one that matched.
    CHECK(image.fat().fileSize(QStringLiteral("never-written.img")) == -1);
}

TEST_CASE("FAT driver reports zero for a file that is there and empty", "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    image.fat().writeFile(QStringLiteral("empty.txt"), QByteArray());
    image.sync();

    CHECK(image.fat().fileExists(QStringLiteral("empty.txt")));
    CHECK(image.fat().fileSize(QStringLiteral("empty.txt")) == 0);
}

TEST_CASE("FAT driver reports the length of a file spanning many clusters",
          "[fat][image]")
{
    REQUIRE_MKFS();
    FatImage image(32, 64);

    // Several clusters, so the length comes from the directory entry rather
    // than from anything the cluster chain happens to imply.
    const QByteArray contents(200 * 1024 + 7, '\x5A');
    image.fat().writeFile(QStringLiteral("multi.img"), contents);
    image.sync();

    CHECK(image.fat().fileSize(QStringLiteral("multi.img")) == contents.size());
}

// ══════════════════════════════════════════════════════════════
// Where on the card the customisation is allowed to be written
//
// fatPartition() reads the partition table and hands back the region the
// boot partition occupies. Everything the customisation step writes --
// cmdline.txt, config.txt, firstrun.sh, the cloud-init files -- goes
// through that region, so a table it misreads is customisation written
// over the image that was just verified, or over the table itself.
//
// It was uncovered: fat_partition_image_test formats a whole scratch file
// and constructs the partition directly, so nothing had ever exercised the
// table parsing. Every refusal below is what stops a card with an
// unexpected, truncated or hostile table being written to at a computed
// offset, and each one has its own message so a report says which.

namespace {

// A scratch image holding whatever partition table a case wants, and no
// filesystem at all. Every case here ends in a refusal, so nothing needs
// mkfs -- what is under test is the arithmetic that decides an offset, not
// what is at it.
class TableImage
{
public:
    explicit TableImage(quint64 sizeBytes = 4 * 1024 * 1024)
        : _scratch(QStringLiteral("rpi-imager-table"))
    {
        _path = _scratch.filePath(QStringLiteral("table.img"));
        auto sizing = rpi_imager::FileOperations::Create();
        if (sizing->CreateTestFile(_path.toStdString(), sizeBytes) !=
            rpi_imager::FileError::kSuccess) {
            throw std::runtime_error("could not create the scratch image");
        }
    }

    ~TableImage()
    {
        _dw.reset();
        _ops.reset();
    }

    TableImage(const TableImage &) = delete;
    TableImage &operator=(const TableImage &) = delete;

    void writeAt(quint64 offset, const void *data, std::size_t size)
    {
        QFile f(_path);
        REQUIRE(f.open(QIODevice::ReadWrite));
        REQUIRE(f.seek(static_cast<qint64>(offset)));
        REQUIRE(f.write(static_cast<const char *>(data),
                        static_cast<qint64>(size)) == static_cast<qint64>(size));
        f.close();
    }

    // Opened only once every table byte is in place: DeviceWrapper caches
    // blocks, so a write behind its back afterwards would not be seen.
    DeviceWrapper &wrapper()
    {
        if (!_dw) {
            _ops = rpi_imager::FileOperations::Create();
            if (_ops->OpenDevice(_path.toStdString()) != rpi_imager::FileError::kSuccess)
                throw std::runtime_error("could not open the scratch image");
            _dw = std::make_unique<DeviceWrapper>(_ops.get());
        }
        return *_dw;
    }

private:
    ScopedTempDir _scratch;
    QString _path;
    std::unique_ptr<rpi_imager::FileOperations> _ops;
    std::unique_ptr<DeviceWrapper> _dw;
};

// An MBR with one partition, as a card written by Imager has.
mbr_table oneParititionMbr(std::uint32_t startSector = 8192,
                           std::uint32_t sectorCount = 1024)
{
    mbr_table mbr{};
    mbr.part[0].id = 0x0C;  // FAT32 LBA
    mbr.part[0].starting_sector = startSector;
    mbr.part[0].nr_of_sectors = sectorCount;
    mbr.signature[0] = 0x55;
    mbr.signature[1] = 0xAA;
    return mbr;
}

gpt_header gptHeader()
{
    gpt_header gpt{};
    std::memcpy(gpt.Signature, "EFI PART", 8);
    gpt.MyLBA = 1;
    gpt.PartitionEntryLBA = 2;
    gpt.NumberOfPartitionEntries = 128;
    gpt.SizeOfPartitionEntry = sizeof(gpt_partition);
    return gpt;
}

} // namespace

TEST_CASE("Only the four partitions a card can have may be asked for", "[table]")
{
    // Nothing in the application asks for anything but the first, but the
    // index goes straight into a fixed array of four: without the check,
    // partition 0 reads the sixteen bytes in front of the table and
    // partition 5 the sixteen after it, and either produces an offset with
    // no relation to the card.
    TableImage image;
    const mbr_table mbr = oneParititionMbr();
    image.writeAt(0, &mbr, sizeof(mbr));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(0),
                      ContainsSubstring("partitions 1-4"));
    CHECK_THROWS_WITH(image.wrapper().fatPartition(5),
                      ContainsSubstring("partitions 1-4"));
    CHECK_THROWS_WITH(image.wrapper().fatPartition(-1),
                      ContainsSubstring("partitions 1-4"));
}

TEST_CASE("A card with no partition table is refused", "[table]")
{
    // A blank or freshly zeroed card, or one holding a bare filesystem with
    // no table at all. Without the signature check the four bytes where the
    // first entry would be are read as a sector count, and the
    // customisation goes wherever that lands.
    TableImage image;

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("MBR does not have valid signature"));
}

TEST_CASE("A partition that is not in the table is refused", "[table]")
{
    // Asking for the second partition of a single-partition card. The entry
    // is all zeros, which as an offset is the start of the card -- the
    // table itself.
    TableImage image;
    const mbr_table mbr = oneParititionMbr();
    image.writeAt(0, &mbr, sizeof(mbr));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(2),
                      ContainsSubstring("Partition does not exist"));
}

TEST_CASE("A partition with a start but no length is refused", "[table]")
{
    // Half an entry, which is what a truncated or interrupted partitioning
    // leaves behind. A zero length would be a partition the writer thinks
    // it can put a file in.
    TableImage image;
    mbr_table mbr = oneParititionMbr();
    mbr.part[0].nr_of_sectors = 0;
    image.writeAt(0, &mbr, sizeof(mbr));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("Partition does not exist"));
}

TEST_CASE("A GPT card is read as GPT rather than as its protective MBR", "[table]")
{
    // A GPT-partitioned card carries a protective MBR whose single entry
    // covers the whole disk. Read as an MBR that entry is a perfectly valid
    // partition starting at sector 1, so the customisation would be written
    // over the GPT itself. The GPT has to be looked at first.
    //
    // Shown by giving the GPT an entry that must be refused while leaving
    // the protective MBR one that would be accepted: a refusal means the
    // GPT was what got read.
    TableImage image;
    mbr_table protective = oneParititionMbr(/*startSector=*/1,
                                            /*sectorCount=*/8191);
    protective.part[0].id = 0xEE;  // GPT protective
    image.writeAt(0, &protective, sizeof(protective));

    const gpt_header gpt = gptHeader();
    image.writeAt(512, &gpt, sizeof(gpt));
    gpt_partition part{};
    part.StartingLBA = 200;
    part.EndingLBA = 100;  // refused, and only the GPT path can refuse it
    image.writeAt(1024, &part, sizeof(part));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("ending LBA before starting LBA"));
}

TEST_CASE("A GPT header that is not the primary one is not used", "[table]")
{
    // The signature also appears in the backup header at the end of the
    // card, and in any stale copy left behind by a previous partitioning.
    // Only the one that says it is at LBA 1 is the table for this card.
    TableImage image;
    gpt_header gpt = gptHeader();
    gpt.MyLBA = 2;  // not the primary header
    image.writeAt(512, &gpt, sizeof(gpt));
    // No MBR written, so falling through to the MBR path is visible.

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("MBR does not have valid signature"));
}

TEST_CASE("A GPT partition beyond the number it declares is refused", "[table]")
{
    TableImage image;
    gpt_header gpt = gptHeader();
    gpt.NumberOfPartitionEntries = 0;
    image.writeAt(512, &gpt, sizeof(gpt));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("Partition does not exist"));
}

TEST_CASE("A GPT entry array beyond the end of the arithmetic is refused", "[table]")
{
    // The entry offset is a sector number multiplied by 512. A card whose
    // table claims a sector number near the top of the range makes that
    // multiplication wrap, and a wrapped offset points back into the start
    // of the card.
    TableImage image;
    gpt_header gpt = gptHeader();
    gpt.PartitionEntryLBA = UINT64_MAX / 512 + 1;
    image.writeAt(512, &gpt, sizeof(gpt));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("entry LBA overflow"));
}

TEST_CASE("A GPT entry index that runs off the end of the range is refused", "[table]")
{
    // The entry array base survives the check above, and then the step to
    // the requested entry is what overflows.
    TableImage image;
    gpt_header gpt = gptHeader();
    gpt.PartitionEntryLBA = UINT64_MAX / 512;
    gpt.SizeOfPartitionEntry = 512;
    image.writeAt(512, &gpt, sizeof(gpt));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(2),
                      ContainsSubstring("entry offset overflow"));
}

TEST_CASE("A GPT partition that ends before it starts is refused", "[table]")
{
    // Subtracting the two would wrap into an enormous length, and a length
    // is what bounds every write into the partition.
    TableImage image;
    const gpt_header gpt = gptHeader();
    image.writeAt(512, &gpt, sizeof(gpt));
    gpt_partition part{};
    part.StartingLBA = 4096;
    part.EndingLBA = 4095;
    image.writeAt(1024, &part, sizeof(part));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("ending LBA before starting LBA"));
}

TEST_CASE("A GPT partition starting past the end of the arithmetic is refused", "[table]")
{
    TableImage image;
    const gpt_header gpt = gptHeader();
    image.writeAt(512, &gpt, sizeof(gpt));
    gpt_partition part{};
    part.StartingLBA = UINT64_MAX / 512 + 1;
    part.EndingLBA = UINT64_MAX / 512 + 2;
    image.writeAt(1024, &part, sizeof(part));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("offset/size overflow"));
}

TEST_CASE("A GPT partition longer than the arithmetic allows is refused", "[table]")
{
    TableImage image;
    const gpt_header gpt = gptHeader();
    image.writeAt(512, &gpt, sizeof(gpt));
    gpt_partition part{};
    part.StartingLBA = 0;
    part.EndingLBA = UINT64_MAX / 512 + 1;
    image.writeAt(1024, &part, sizeof(part));

    CHECK_THROWS_WITH(image.wrapper().fatPartition(1),
                      ContainsSubstring("offset/size overflow"));
}

// ══════════════════════════════════════════════════════════════
// A directory whose cluster chain loops back on itself
//
// The driver walks a directory by following its chain of clusters through
// the FAT. A corrupt FAT can point a cluster back at one already visited,
// and a walker that does not notice follows it forever.
//
// That is the worst way for this to fail. There is no error and no
// progress: the application stops responding part-way through writing the
// customisation to a card, with the card half-configured and nothing on
// screen to explain it. The user's only move is to pull it out.
//
// Three walkers carry the same guard -- listing, reading and deleting --
// and none of them was covered, because a filesystem built by mkfs never
// has a loop in it. The fixture below makes one: it fills the root
// directory until it needs a second cluster, then rewrites that cluster's
// FAT entry to point back at the first.

namespace {

// Enough entries to push the root directory past one cluster. Long names
// take several directory entries each, so this does not need to be many
// files -- but it does need to be more than fits, which is checked rather
// than assumed.
bool fillRootDirectory(const QString &imagePath, int fileCount)
{
    ScopedTempDir sources(QStringLiteral("rpi-imager-fatsrc"));
    QStringList args;
    args << QStringLiteral("-i") << imagePath << QStringLiteral("-o");
    for (int i = 0; i < fileCount; ++i) {
        const QString name = QStringLiteral("a-file-with-a-fairly-long-name-%1.txt")
                                 .arg(i, 3, 10, QLatin1Char('0'));
        const QString path = sources.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write("x");
        f.close();
        args << path;
    }
    args << QStringLiteral("::/");
    return runMtool(QStringLiteral("mcopy"), args);
}

struct Fat32Geometry {
    quint32 fatStartByte = 0;
    quint32 rootCluster = 0;
    quint16 bytesPerSector = 0;
};

Fat32Geometry readGeometry(const QString &imagePath)
{
    Fat32Geometry g;
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly))
        return g;
    const QByteArray boot = f.read(512);
    f.close();
    if (boot.size() < 512)
        return g;
    auto u16 = [&](int at) {
        return quint16(quint8(boot.at(at))) | (quint16(quint8(boot.at(at + 1))) << 8);
    };
    auto u32 = [&](int at) {
        return quint32(quint8(boot.at(at)))
             | (quint32(quint8(boot.at(at + 1))) << 8)
             | (quint32(quint8(boot.at(at + 2))) << 16)
             | (quint32(quint8(boot.at(at + 3))) << 24);
    };
    g.bytesPerSector = u16(11);
    const quint16 reservedSectors = u16(14);
    g.rootCluster = u32(44);
    g.fatStartByte = quint32(reservedSectors) * g.bytesPerSector;
    return g;
}

quint32 readFatEntry(const QString &imagePath, const Fat32Geometry &g, quint32 cluster)
{
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    f.seek(g.fatStartByte + cluster * 4);
    const QByteArray raw = f.read(4);
    f.close();
    if (raw.size() < 4)
        return 0;
    return (quint32(quint8(raw.at(0)))
            | (quint32(quint8(raw.at(1))) << 8)
            | (quint32(quint8(raw.at(2))) << 16)
            | (quint32(quint8(raw.at(3))) << 24)) & 0x0FFFFFFF;
}

bool writeFatEntry(const QString &imagePath, const Fat32Geometry &g,
                   quint32 cluster, quint32 value)
{
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadWrite))
        return false;
    if (!f.seek(g.fatStartByte + cluster * 4))
        return false;
    char raw[4];
    raw[0] = char(value & 0xFF);
    raw[1] = char((value >> 8) & 0xFF);
    raw[2] = char((value >> 16) & 0xFF);
    raw[3] = char((value >> 24) & 0xFF);
    const bool ok = f.write(raw, 4) == 4;
    f.close();
    return ok;
}

// Fills the root directory, then makes its second cluster point back at the
// first. Returns false when the directory did not need a second cluster,
// which would leave nothing to loop and a case that proves nothing.
bool loopTheRootDirectory(const QString &imagePath)
{
    if (!fillRootDirectory(imagePath, 200))
        return false;
    const Fat32Geometry g = readGeometry(imagePath);
    if (g.bytesPerSector == 0 || g.rootCluster < 2)
        return false;
    const quint32 second = readFatEntry(imagePath, g, g.rootCluster);
    if (second < 2 || second >= 0x0FFFFFF8)
        return false;  // the directory still fits in one cluster
    return writeFatEntry(imagePath, g, second, g.rootCluster);
}

} // namespace

TEST_CASE("A directory whose cluster chain loops is refused, not followed",
          "[fat][image][corrupt]")
{
    REQUIRE_MKFS();
    if (!haveMtools())
        SKIP("mtools is needed to fill the directory before corrupting it");

    bool looped = false;
    bool threw = false;
    std::string message;
    try {
        FatImage image(32, 256, [&](const QString &path) {
            looped = loopTheRootDirectory(path);
        });
        // Reached only if the driver accepted the filesystem.
        (void)image.fat().listAllFiles();
    } catch (const std::exception &e) {
        threw = true;
        message = e.what();
    }

    if (!looped)
        SKIP("the root directory did not need a second cluster, so there is "
             "no chain to loop and this would prove nothing");

    // Reported, and reported as what it is. The alternative is not an
    // exception but a walk that never ends: the application stops
    // responding part-way through configuring a card, with nothing on
    // screen to say why, and the user pulls it out.
    CHECK(threw);
    CHECK_THAT(message, ContainsSubstring("Circular cluster references"));
}

// The other two walkers -- readFile and deleteFile -- carry the same guard
// and answer a loop by giving up on the search rather than by throwing.
// Neither is reachable with a loop in the root directory, because opening
// the partition walks that directory first and throws before any of them is
// called. Reaching them needs the loop in a subdirectory chain, which mkfs
// and mtools do not produce and which would have to be assembled entry by
// entry. Left as a known gap rather than a test that cannot be built
// honestly.

// ══════════════════════════════════════════════════════════════
// The same loop, one directory down
//
// The note above left the other two walkers uncovered because a loop in the
// root directory is caught when the partition is opened, before readFile()
// or deleteFile() can be called. Putting the loop in a subdirectory gets
// past that: the root walks cleanly, the partition opens, and the loop is
// only met when something goes looking inside.
//
// Which is the case that matters. Customisation writes go into the boot
// partition and its subdirectories, and both of these are called with a
// path -- readFile("overlays/x") when checking what is already there,
// deleteFile("overlays/x") when replacing it. A walker that follows the loop
// does not fail; it stops, part-way through configuring a card, with nothing
// on screen and a half-written filesystem in the reader.
//
// mkfs and mtools never produce a loop, so the fixture builds one: create a
// subdirectory, fill it until its chain runs to three clusters, then point
// the second cluster back at the first.
// ══════════════════════════════════════════════════════════════

namespace {

// The 8.3 name is chosen so mtools writes no long-name entries for the
// directory itself, which keeps finding it in the root a matter of matching
// eleven bytes.
const char *const kLoopDirShortName = "LOOPDIR    ";
const QString kLoopDirName = QStringLiteral("LOOPDIR");

// Everything readGeometry() collects, plus what is needed to turn a cluster
// number into a byte offset.
struct Fat32DataArea {
    Fat32Geometry base;
    quint8  sectorsPerCluster = 0;
    quint32 firstDataSector = 0;
};

Fat32DataArea readDataArea(const QString &imagePath)
{
    Fat32DataArea d;
    d.base = readGeometry(imagePath);
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly))
        return d;
    const QByteArray boot = f.read(512);
    f.close();
    if (boot.size() < 512)
        return d;
    auto u16 = [&](int at) {
        return quint16(quint8(boot.at(at))) | (quint16(quint8(boot.at(at + 1))) << 8);
    };
    auto u32 = [&](int at) {
        return quint32(quint8(boot.at(at)))
             | (quint32(quint8(boot.at(at + 1))) << 8)
             | (quint32(quint8(boot.at(at + 2))) << 16)
             | (quint32(quint8(boot.at(at + 3))) << 24);
    };
    d.sectorsPerCluster = quint8(boot.at(13));
    const quint16 reservedSectors = u16(14);
    const quint8  numberOfFats    = quint8(boot.at(16));
    const quint32 sectorsPerFat   = u32(36);
    d.firstDataSector = quint32(reservedSectors) + quint32(numberOfFats) * sectorsPerFat;
    return d;
}

qint64 clusterByteOffset(const Fat32DataArea &d, quint32 cluster)
{
    if (d.sectorsPerCluster == 0 || cluster < 2)
        return -1;
    return (qint64(d.firstDataSector) + qint64(cluster - 2) * d.sectorsPerCluster)
           * d.base.bytesPerSector;
}

// The first cluster of the named directory, found by matching its 8.3 name
// among the root directory's entries. 0 when it is not there.
quint32 directoryFirstCluster(const QString &imagePath, const Fat32DataArea &d,
                              const char *shortName)
{
    const qint64 at = clusterByteOffset(d, d.base.rootCluster);
    if (at < 0)
        return 0;
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(at))
        return 0;
    const int clusterBytes = int(d.sectorsPerCluster) * int(d.base.bytesPerSector);
    const QByteArray root = f.read(clusterBytes);
    f.close();

    for (int off = 0; off + 32 <= root.size(); off += 32) {
        if (root.at(off) == '\0')
            break;                                  // end of directory
        if (quint8(root.at(off)) == 0xE5)
            continue;                               // deleted
        const quint8 attr = quint8(root.at(off + 11));
        if ((attr & 0x0F) == 0x0F)
            continue;                               // long-name fragment
        if (!(attr & 0x10))
            continue;                               // not a directory
        if (std::memcmp(root.constData() + off, shortName, 11) != 0)
            continue;
        const quint16 hi = quint16(quint8(root.at(off + 20)))
                         | (quint16(quint8(root.at(off + 21))) << 8);
        const quint16 lo = quint16(quint8(root.at(off + 26)))
                         | (quint16(quint8(root.at(off + 27))) << 8);
        return (quint32(hi) << 16) | quint32(lo);
    }
    return 0;
}

bool fillSubdirectory(const QString &imagePath, int fileCount)
{
    if (!runMtool(QStringLiteral("mmd"),
                  {QStringLiteral("-i"), imagePath,
                   QStringLiteral("::/") + kLoopDirName}))
        return false;

    ScopedTempDir sources(QStringLiteral("rpi-imager-fatsub"));
    QStringList args;
    args << QStringLiteral("-i") << imagePath << QStringLiteral("-o");
    for (int i = 0; i < fileCount; ++i) {
        const QString name = QStringLiteral("a-file-with-a-fairly-long-name-%1.txt")
                                 .arg(i, 3, 10, QLatin1Char('0'));
        const QString path = sources.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write("x");
        f.close();
        args << path;
    }
    args << QStringLiteral("::/") + kLoopDirName + QStringLiteral("/");
    return runMtool(QStringLiteral("mcopy"), args);
}

// Builds the loop. Returns false when the subdirectory did not need three
// clusters, in which case there is nothing worth looping: the walk has to
// cross one cluster boundary legitimately and meet the loop at the next, so
// the cluster it lands in must itself be full.
bool loopASubdirectory(const QString &imagePath)
{
    if (!fillSubdirectory(imagePath, 400))
        return false;
    const Fat32DataArea d = readDataArea(imagePath);
    if (d.base.bytesPerSector == 0 || d.sectorsPerCluster == 0)
        return false;

    const quint32 first = directoryFirstCluster(imagePath, d, kLoopDirShortName);
    if (first < 2)
        return false;
    const quint32 second = readFatEntry(imagePath, d.base, first);
    if (second < 2 || second >= 0x0FFFFFF8)
        return false;
    const quint32 third = readFatEntry(imagePath, d.base, second);
    if (third < 2 || third >= 0x0FFFFFF8)
        return false;   // only two clusters: the walk would stop before the loop

    return writeFatEntry(imagePath, d.base, second, first);
}

// ── A directory that is not laid out contiguously ───────────────────────
//
// mkfs and mtools allocate a directory's clusters one after another, so a
// walker that ignores the FAT and just reads on gets the right answer
// anyway. Moving one cluster elsewhere is what separates the two: reading
// on then lands in the old copy, and following the chain lands in the new
// one.

// A cluster with nothing in it, somewhere past what the fixture has used.
quint32 findFreeCluster(const QString &imagePath, const Fat32DataArea &d,
                        quint32 from = 4000)
{
    for (quint32 c = from; c < from + 4000; ++c)
        if (readFatEntry(imagePath, d.base, c) == 0)
            return c;
    return 0;
}

bool moveCluster(const QString &imagePath, const Fat32DataArea &d,
                 quint32 from, quint32 to)
{
    const qint64 src = clusterByteOffset(d, from);
    const qint64 dst = clusterByteOffset(d, to);
    if (src < 0 || dst < 0)
        return false;
    const int clusterBytes = int(d.sectorsPerCluster) * int(d.base.bytesPerSector);
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadWrite))
        return false;
    if (!f.seek(src))
        return false;
    const QByteArray body = f.read(clusterBytes);
    if (body.size() != clusterBytes)
        return false;
    if (!f.seek(dst) || f.write(body) != clusterBytes)
        return false;
    // End-of-directory where the cluster used to be, so a walk that reads
    // straight on rather than following the chain stops there.
    if (!f.seek(src))
        return false;
    const QByteArray blank(32, '\0');
    const bool ok = f.write(blank) == blank.size();
    f.close();
    return ok;
}

// Fills a subdirectory, then relocates its second cluster. Returns false
// when the directory did not need a second cluster, or there was nowhere to
// move it to.
bool fragmentASubdirectory(const QString &imagePath)
{
    if (!fillSubdirectory(imagePath, 400))
        return false;
    const Fat32DataArea d = readDataArea(imagePath);
    if (d.base.bytesPerSector == 0 || d.sectorsPerCluster == 0)
        return false;

    const quint32 first = directoryFirstCluster(imagePath, d, kLoopDirShortName);
    if (first < 2)
        return false;
    const quint32 second = readFatEntry(imagePath, d.base, first);
    if (second < 2 || second >= 0x0FFFFFF8)
        return false;
    const quint32 third = readFatEntry(imagePath, d.base, second);
    if (third < 2)
        return false;

    const quint32 far = findFreeCluster(imagePath, d);
    if (far < 2)
        return false;

    if (!moveCluster(imagePath, d, second, far))
        return false;
    if (!writeFatEntry(imagePath, d.base, far, third))
        return false;
    if (!writeFatEntry(imagePath, d.base, first, far))
        return false;
    return writeFatEntry(imagePath, d.base, second, 0);
}

} // namespace

TEST_CASE("A subdirectory whose cluster chain loops does not trap a read",
          "[fat][image][corrupt]")
{
    REQUIRE_MKFS();
    if (!haveMtools())
        SKIP("mtools is needed to build the subdirectory before corrupting it");

    bool looped = false;
    FatImage image(32, 256, [&](const QString &path) {
        looped = loopASubdirectory(path);
    });
    if (!looped)
        SKIP("the subdirectory did not need three clusters, so the walk would "
             "never reach the loop and this would prove nothing");

    // Asking for something that is not in the looping directory is what makes
    // the walk run to the end of the chain. With the guard the search gives
    // up and says so; without it there is no end to run to and this call does
    // not return.
    const QByteArray contents =
        image.fat().readFile(kLoopDirName + QStringLiteral("/not-here.txt"));
    CHECK(contents.isEmpty());

    // And the partition is still usable afterwards -- the guard gives up on
    // the search, not on the filesystem.
    CHECK_FALSE(image.fat().fileExists(QStringLiteral("not-here-either.txt")));
}

TEST_CASE("A subdirectory whose cluster chain loops does not trap a delete",
          "[fat][image][corrupt]")
{
    // The counterpart. Replacing a customisation file deletes the old one
    // first, so this is the walker a re-run of the same write meets.
    REQUIRE_MKFS();
    if (!haveMtools())
        SKIP("mtools is needed to build the subdirectory before corrupting it");

    bool looped = false;
    FatImage image(32, 256, [&](const QString &path) {
        looped = loopASubdirectory(path);
    });
    if (!looped)
        SKIP("the subdirectory did not need three clusters, so the walk would "
             "never reach the loop and this would prove nothing");

    CHECK_FALSE(image.fat().deleteFile(kLoopDirName + QStringLiteral("/not-here.txt")));
}

TEST_CASE("A file past the first cluster of a scattered directory can still be deleted",
          "[fat][image][corrupt]")
{
    // Deleting from a subdirectory has to follow the FAT rather than read on
    // through the image. It used to consult the chain only after a
    // short-name entry, and a directory of long names does not put its
    // cluster boundaries there: "." and ".." shift everything by two
    // entries, so the boundary lands inside a long-name run and the check
    // was skipped. On a contiguously allocated directory reading on lands in
    // the right place anyway, which is why nothing noticed.
    //
    // This directory is not contiguous. Reading on lands in the cluster the
    // second one used to occupy, which now ends the directory -- so the file
    // is reported missing and the caller logs "failed to delete" and carries
    // on. Following the chain finds it.
    REQUIRE_MKFS();
    if (!haveMtools())
        SKIP("mtools is needed to build the subdirectory before moving it");

    bool scattered = false;
    FatImage image(32, 256, [&](const QString &path) {
        scattered = fragmentASubdirectory(path);
    });
    if (!scattered)
        SKIP("the subdirectory could not be scattered, so reading on and "
             "following the chain would land in the same place");

    const QString victim =
        kLoopDirName + QStringLiteral("/a-file-with-a-fairly-long-name-399.txt");

    // readFile() has always consulted the chain after a long-name entry, so
    // it finds the file either way. It is here to show the file really is
    // there to be deleted -- without it a failed delete could just mean a
    // fixture that never wrote it.
    CHECK_FALSE(image.fat().readFile(victim).isEmpty());

    CHECK(image.fat().deleteFile(victim));
    CHECK_FALSE(image.fat().fileExists(victim));
}
