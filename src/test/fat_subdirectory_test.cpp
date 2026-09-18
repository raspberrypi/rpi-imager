/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Writing into a subdirectory, checked against mtools.
 *
 * The driver has been wrong here in a way nothing noticed: an entry meant
 * for a subdirectory was created in the root instead, so a file asked for at
 * "overlays/added.dtbo" was readable at "added.dtbo" and absent from where
 * it was put. Every test that used this driver to read back what this driver
 * had written agreed with it.
 *
 * So it does not mark its own work. mtools builds the reference, and the two
 * check each other both ways: what we write is listed and extracted by
 * mtools, and what mtools writes is listed and read by us. A file in the
 * wrong directory fails the first; a directory we cannot walk fails the
 * second.
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

TEST_CASE("A file more than one directory down is not read yet",
          "[fat][subdir][reference]")
{
    // Found by the reference rather than assumed: readFile() descends
    // exactly one level. It takes the first path component as the directory
    // and looks for the rest as a name inside it, so "a/b/c/deep.bin" is
    // searched for as a file called "b/c/deep.bin" in "a", and is not there.
    //
    // Written down as it stands rather than left failing. A boot partition
    // is one level deep so nothing ships broken by it -- but anything nested
    // further reads as absent, which is worse than an error, and the writer
    // has the matching gap.
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

    // And we do not find it. When the driver learns to descend further, this
    // fails, and this is the place to say so.
    CHECK(fat.readFile(QStringLiteral("a/b/c/deep.bin")).isEmpty());
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

TEST_CASE("A file we cannot place is refused, not put somewhere else",
          "[fat][subdir][reference]")
{
    // The failure this area had, stated as a requirement. Until the writer
    // can create a directory, asking it for a path with one in it must
    // refuse -- and above all must not leave the file in the root, where the
    // caller would never look and mtools would plainly show it.
    REQUIRE_MTOOLS_REFERENCE();

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString img = scratch.filePath(QStringLiteral("ours.img"));

    {
        OurImage image(img);
        REQUIRE(image.ok());
        image.fat().writeFile(QStringLiteral("config.txt"), "arm_64bit=1\n");

        bool threw = false;
        try {
            image.fat().writeFile(QStringLiteral("overlays/one.dtbo"), "ONE");
        } catch (const std::runtime_error &) {
            threw = true;
        }
        CHECK(threw);
        image.close();
    }

    const QStringList paths = rpi_test::mtoolsPaths(img);
    INFO("mtools sees: " << paths.join(QStringLiteral(", ")).toStdString());
    // The refusal left the image as it was.
    CHECK(paths.contains(QStringLiteral("config.txt")));
    // And nothing anywhere called one.dtbo -- not in overlays/, and not
    // dropped in the root under its own name, which is what used to happen.
    for (const QString &path : paths) {
        INFO("found " << path.toStdString());
        CHECK_FALSE(path.endsWith(QStringLiteral("one.dtbo")));
    }
}
