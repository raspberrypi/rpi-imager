/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Writing into a subdirectory, checked against mtools.
 *
 * The driver does not mark its own work here: a case that writes with this
 * driver and reads back with it will agree with itself however wrong the
 * on-disk result is. mtools is the second implementation, and the two check
 * each other both ways -- what we write is listed and extracted by mtools,
 * what mtools writes is listed and read by us. A file in the wrong directory
 * fails the first; a directory we cannot walk fails the second.
 */

#include <catch2/catch_test_macros.hpp>

#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "disk_formatter.h"
#include "fat_reference.h"
#include "file_operations.h"
#include "platform_tools.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <memory>

namespace {

constexpr int kImageMegabytes = 64;

#define REQUIRE_MTOOLS_REFERENCE()                                            \
    if (!rpi_test::haveMtoolsReference())                                     \
    SKIP("mtools is needed to build the reference this is checked against")

// A FAT32 image made by our own formatter, with the driver open on it.
class OurImage
{
public:
    explicit OurImage(const QString &path) : _path(path)
    {
        rpi_imager::DiskFormatter formatter;
        const auto made = formatter.FormatFilesystemOnly(
            path.toStdString(),
            static_cast<std::uint64_t>(kImageMegabytes) * 1024 * 1024);
        _formatted = static_cast<bool>(made);
        if (!_formatted)
            return;

        _ops = rpi_imager::FileOperations::Create();
        if (_ops->OpenDevice(path.toStdString()) != rpi_imager::FileError::kSuccess) {
            _formatted = false;
            return;
        }
        _dw = std::make_unique<DeviceWrapper>(_ops.get());
        _fat = std::make_unique<DeviceWrapperFatPartition>(
            _dw.get(), 0, quint64(kImageMegabytes) * 1024 * 1024);
    }

    ~OurImage()
    {
        _fat.reset();
        _dw.reset();
        _ops.reset();
    }

    bool ok() const { return _formatted; }
    DeviceWrapperFatPartition &fat() { return *_fat; }
    void sync() { _dw->sync(); }

    // Close the handles so mtools can read the file on Windows.
    void close()
    {
        if (_dw)
            _dw->sync();
        _fat.reset();
        _dw.reset();
        _ops.reset();
    }

private:
    QString _path;
    bool _formatted = false;
    std::unique_ptr<rpi_imager::FileOperations> _ops;
    std::unique_ptr<DeviceWrapper> _dw;
    std::unique_ptr<DeviceWrapperFatPartition> _fat;
};

} // namespace

TEST_CASE("The reference itself holds what it was asked to hold",
          "[fat][subdir][reference]")
{
    // Before any of our code is trusted, the thing it will be measured
    // against has to be right. Built by mtools, read back by mtools.
    REQUIRE_MTOOLS_REFERENCE();

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString img = scratch.filePath(QStringLiteral("reference.img"));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    files.insert(QStringLiteral("overlays/one.dtbo"), "ONE");
    files.insert(QStringLiteral("a/b/c/deep.bin"), "DEEP");

    REQUIRE(rpi_test::buildMtoolsReference(img, kImageMegabytes, files,
                                           scratch.path()));

    const QStringList paths = rpi_test::mtoolsPaths(img);
    INFO("mtools sees: " << paths.join(QStringLiteral(", ")).toStdString());
    CHECK(paths.contains(QStringLiteral("config.txt")));
    CHECK(paths.contains(QStringLiteral("overlays/one.dtbo")));
    CHECK(paths.contains(QStringLiteral("a/b/c/deep.bin")));

    CHECK(rpi_test::mtoolsRead(img, QStringLiteral("overlays/one.dtbo"),
                               scratch.path()) == QByteArray("ONE"));
    CHECK(rpi_test::mtoolsRead(img, QStringLiteral("a/b/c/deep.bin"),
                               scratch.path()) == QByteArray("DEEP"));
}

TEST_CASE("Our driver reads what mtools put in a subdirectory",
          "[fat][subdir][reference]")
{
    // Theirs, read by ours. A directory the driver cannot walk fails here,
    // and this half already worked -- reading has been able to descend for
    // some time. It is the control for the half that has not.
    REQUIRE_MTOOLS_REFERENCE();

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString img = scratch.filePath(QStringLiteral("reference.img"));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("config.txt"), "arm_64bit=1\n");
    files.insert(QStringLiteral("overlays/one.dtbo"), "ONE");
    files.insert(QStringLiteral("a/b/c/deep.bin"), "DEEP");
    REQUIRE(rpi_test::buildMtoolsReference(img, kImageMegabytes, files,
                                           scratch.path()));

    auto ops = rpi_imager::FileOperations::Create();
    REQUIRE(ops->OpenDevice(img.toStdString()) == rpi_imager::FileError::kSuccess);
    DeviceWrapper dw(ops.get());
    DeviceWrapperFatPartition fat(&dw, 0,
                                  quint64(kImageMegabytes) * 1024 * 1024);

    // One level down, which is the shape a boot partition has: overlays/,
    // and nothing under it.
    CHECK(fat.readFile(QStringLiteral("config.txt")) ==
          files.value(QStringLiteral("config.txt")));
    CHECK(fat.readFile(QStringLiteral("overlays/one.dtbo")) == QByteArray("ONE"));
}

TEST_CASE("A file more than one directory down is read at its path",
          "[fat][subdir][reference]")
{
    // Depth is checked against the reference rather than assumed. A driver
    // that descends only one level reports anything below it as absent,
    // which a caller cannot tell from a file that is genuinely not there.
    REQUIRE_MTOOLS_REFERENCE();

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString img = scratch.filePath(QStringLiteral("reference.img"));

    QMap<QString, QByteArray> files;
    files.insert(QStringLiteral("a/b/c/deep.bin"), "DEEP");
    REQUIRE(rpi_test::buildMtoolsReference(img, kImageMegabytes, files,
                                           scratch.path()));

    // mtools put it where it was asked to.
    REQUIRE(rpi_test::mtoolsPaths(img).contains(QStringLiteral("a/b/c/deep.bin")));
    REQUIRE(rpi_test::mtoolsRead(img, QStringLiteral("a/b/c/deep.bin"),
                                 scratch.path()) == QByteArray("DEEP"));

    auto ops = rpi_imager::FileOperations::Create();
    REQUIRE(ops->OpenDevice(img.toStdString()) == rpi_imager::FileError::kSuccess);
    DeviceWrapper dw(ops.get());
    DeviceWrapperFatPartition fat(&dw, 0,
                                  quint64(kImageMegabytes) * 1024 * 1024);

    // And so do we, at the path mtools put it at and at no other.
    CHECK(fat.readFile(QStringLiteral("a/b/c/deep.bin")) == QByteArray("DEEP"));
    CHECK(fat.fileExists(QStringLiteral("a/b/c/deep.bin")));
    CHECK(fat.readFile(QStringLiteral("deep.bin")).isEmpty());
    CHECK(fat.readFile(QStringLiteral("a/deep.bin")).isEmpty());
}

TEST_CASE("A file we write to the root is where mtools looks for it",
          "[fat][subdir][reference]")
{
    // Ours, read by theirs, for the case that already works. Without this
    // the cross-check could pass by mtools failing to read anything at all.
    REQUIRE_MTOOLS_REFERENCE();

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString img = scratch.filePath(QStringLiteral("ours.img"));

    {
        OurImage image(img);
        REQUIRE(image.ok());
        image.fat().writeFile(QStringLiteral("config.txt"), "arm_64bit=1\n");
        image.close();
    }

    const QStringList paths = rpi_test::mtoolsPaths(img);
    INFO("mtools sees: " << paths.join(QStringLiteral(", ")).toStdString());
    CHECK(paths.contains(QStringLiteral("config.txt")));
    CHECK(rpi_test::mtoolsRead(img, QStringLiteral("config.txt"),
                               scratch.path()) == QByteArray("arm_64bit=1\n"));
}

TEST_CASE("A file we nest is where mtools looks for it, and nowhere else",
          "[fat][subdir][reference]")
{
    // The writer makes the directory and puts the file in it, and above all
    // does not leave it in the root. Only the other implementation can say
    // which of those happened: reading our own image back would agree with
    // whatever we did.
    REQUIRE_MTOOLS_REFERENCE();

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString img = scratch.filePath(QStringLiteral("ours.img"));

    {
        OurImage image(img);
        REQUIRE(image.ok());
        image.fat().writeFile(QStringLiteral("config.txt"), "arm_64bit=1\n");
        image.fat().writeFile(QStringLiteral("overlays/one.dtbo"), "ONE");
        // Two files in one directory, so the second is placed into a
        // directory that already exists rather than one it just made.
        image.fat().writeFile(QStringLiteral("overlays/two.dtbo"), "TWO");
        // And a path several levels down, every directory of which has to be
        // created on the way.
        image.fat().writeFile(QStringLiteral("a/b/c/deep.bin"), "DEEP");
        image.close();
    }

    const QStringList paths = rpi_test::mtoolsPaths(img);
    INFO("mtools sees: " << paths.join(QStringLiteral(", ")).toStdString());
    CHECK(paths.contains(QStringLiteral("config.txt")));
    CHECK(paths.contains(QStringLiteral("overlays/one.dtbo")));
    CHECK(paths.contains(QStringLiteral("overlays/two.dtbo")));
    CHECK(paths.contains(QStringLiteral("a/b/c/deep.bin")));

    // Nothing stranded in the root under its own name.
    CHECK_FALSE(paths.contains(QStringLiteral("one.dtbo")));
    CHECK_FALSE(paths.contains(QStringLiteral("two.dtbo")));
    CHECK_FALSE(paths.contains(QStringLiteral("deep.bin")));

    // And the contents survive the trip, read out by the other implementation.
    CHECK(rpi_test::mtoolsRead(img, QStringLiteral("overlays/one.dtbo"),
                               scratch.path()) == QByteArray("ONE"));
    CHECK(rpi_test::mtoolsRead(img, QStringLiteral("overlays/two.dtbo"),
                               scratch.path()) == QByteArray("TWO"));
    CHECK(rpi_test::mtoolsRead(img, QStringLiteral("a/b/c/deep.bin"),
                               scratch.path()) == QByteArray("DEEP"));
}

TEST_CASE("A directory we make is one mtools can add to", "[fat][subdir][reference]")
{
    // Stronger than reading it back: mtools writing into our directory has
    // to find "." and ".." where the format says they are, the cluster chain
    // terminated, and the end-of-directory marker in place. A directory that
    // merely lists correctly can still be malformed in all three.
    REQUIRE_MTOOLS_REFERENCE();

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString img = scratch.filePath(QStringLiteral("ours.img"));

    {
        OurImage image(img);
        REQUIRE(image.ok());
        image.fat().writeFile(QStringLiteral("overlays/ours.dtbo"), "OURS");
        image.close();
    }

    const QString staged = QStringLiteral("theirs.bin");
    {
        QFile f(QDir(scratch.path()).filePath(staged));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("THEIRS");
    }
    REQUIRE(rpi_test::runMtool(QStringLiteral("mcopy"),
                               {QStringLiteral("-i"), img, staged,
                                QStringLiteral("::/overlays/theirs.bin")},
                               nullptr, scratch.path()));

    const QStringList paths = rpi_test::mtoolsPaths(img);
    INFO("mtools sees: " << paths.join(QStringLiteral(", ")).toStdString());
    CHECK(paths.contains(QStringLiteral("overlays/ours.dtbo")));
    CHECK(paths.contains(QStringLiteral("overlays/theirs.bin")));

    // Both readable by us afterwards, so their write did not disturb ours.
    auto ops = rpi_imager::FileOperations::Create();
    REQUIRE(ops->OpenDevice(img.toStdString()) == rpi_imager::FileError::kSuccess);
    DeviceWrapper dw(ops.get());
    DeviceWrapperFatPartition fat(&dw, 0, quint64(kImageMegabytes) * 1024 * 1024);
    CHECK(fat.readFile(QStringLiteral("overlays/ours.dtbo")) == QByteArray("OURS"));
    CHECK(fat.readFile(QStringLiteral("overlays/theirs.bin")) == QByteArray("THEIRS"));
}
