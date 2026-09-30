/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The filter the Windows open dialog is given.
 *
 * Imager hands IFileDialog a list of COMDLG_FILTERSPEC, each holding two bare
 * pointers into a container the caller owns. The container was a vector, and
 * a vector moves what is already in it when it grows: a short string keeps
 * its characters inside the string object, so pushing the second filter moved
 * the first one's text and left that filter's two pointers addressing freed
 * memory. Two filters was enough, and two is what Imager passes.
 *
 * Nothing could reach this: the only caller opens a dialog and waits for
 * somebody to click in it.
 */

#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QStringList>

#include <windows.h>
#include <shobjidl.h>

#include <deque>
#include <string>
#include <vector>

namespace NativeFileDialogTesting {
std::vector<COMDLG_FILTERSPEC> convertFilter(const QString &qtFilter,
                                             std::deque<std::wstring> &storage);
}

using NativeFileDialogTesting::convertFilter;

namespace {

QString fromWide(const wchar_t *s)
{
    return QString::fromWCharArray(s);
}

} // namespace

TEST_CASE("A single filter becomes one spec", "[filedialog]")
{
    std::deque<std::wstring> storage;
    const auto filters = convertFilter(
        QStringLiteral("Image files (*.img *.zip)"), storage);

    REQUIRE(filters.size() == 1);
    CHECK(fromWide(filters[0].pszName) == QStringLiteral("Image files"));
    // IFileDialog separates patterns with semicolons; Qt uses spaces.
    CHECK(fromWide(filters[0].pszSpec) == QStringLiteral("*.img;*.zip"));
}

TEST_CASE("Every filter still points at its own text", "[filedialog]")
{
    // The case the bug was in. Each spec is checked against the storage entry
    // it is supposed to address, so a pointer left behind by the container
    // growing fails here rather than showing rubbish in the dialog.
    std::deque<std::wstring> storage;
    const auto filters = convertFilter(
        QStringLiteral("Image files (*.img *.zip *.xz *.gz);;All files (*.*)"),
        storage);

    REQUIRE(filters.size() == 2);
    REQUIRE(storage.size() == 4);

    for (std::size_t i = 0; i < filters.size(); ++i) {
        INFO("filter " << i);
        CHECK(filters[i].pszName == storage[i * 2].c_str());
        CHECK(filters[i].pszSpec == storage[i * 2 + 1].c_str());
    }

    CHECK(fromWide(filters[0].pszName) == QStringLiteral("Image files"));
    CHECK(fromWide(filters[0].pszSpec) == QStringLiteral("*.img;*.zip;*.xz;*.gz"));
    CHECK(fromWide(filters[1].pszName) == QStringLiteral("All files"));
    CHECK(fromWide(filters[1].pszSpec) == QStringLiteral("*.*"));
}

TEST_CASE("Many filters all survive being added", "[filedialog]")
{
    // More than a vector's initial capacity, so the old container would have
    // grown several times over and moved nearly every entry.
    QStringList parts;
    for (int i = 0; i < 24; ++i)
        parts << QStringLiteral("Kind %1 (*.e%1)").arg(i);

    std::deque<std::wstring> storage;
    const auto filters = convertFilter(parts.join(QStringLiteral(";;")), storage);

    REQUIRE(filters.size() == 24);
    for (std::size_t i = 0; i < filters.size(); ++i) {
        INFO("filter " << i);
        CHECK(filters[i].pszName == storage[i * 2].c_str());
        CHECK(filters[i].pszSpec == storage[i * 2 + 1].c_str());
        CHECK(fromWide(filters[i].pszName) ==
              QStringLiteral("Kind %1").arg(i));
        CHECK(fromWide(filters[i].pszSpec) == QStringLiteral("*.e%1").arg(i));
    }
}

TEST_CASE("An empty filter offers nothing rather than an empty entry",
          "[filedialog]")
{
    // SetFileTypes is not called at all for an empty list. One blank entry
    // would be a dialog offering a file type with no name and no pattern.
    std::deque<std::wstring> storage;
    CHECK(convertFilter(QString(), storage).empty());
    CHECK(storage.empty());
}

TEST_CASE("A filter with no pattern still names its kind", "[filedialog]")
{
    // Malformed, but it comes from a caller rather than from the user, and
    // the answer has to be a valid spec rather than a pointer to nowhere.
    std::deque<std::wstring> storage;
    const auto filters = convertFilter(QStringLiteral("Everything"), storage);

    REQUIRE(filters.size() == 1);
    CHECK(fromWide(filters[0].pszName) == QStringLiteral("Everything"));
    CHECK(fromWide(filters[0].pszSpec).isEmpty());
    CHECK(filters[0].pszSpec != nullptr);
}
