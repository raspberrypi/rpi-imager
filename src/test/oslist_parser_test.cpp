/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The OS list is the first thing a user sees, and every failure mode here is
 * silent: entries that quietly vanish, the wrong language's list, images
 * offered in an order that hides the one for the attached board. Nothing
 * crashes -- the list just comes out wrong, and there is no error to read.
 *
 * These functions are pure transformations over the repository JSON, so they
 * are exercised directly with hand-written documents rather than by standing
 * up a model and a QML engine.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "oslistparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

using Catch::Matchers::ContainsSubstring;

namespace {

QJsonObject objFromJson(const char *text)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(QByteArray(text), &err);
    REQUIRE(err.error == QJsonParseError::NoError);
    return doc.object();
}

QJsonArray arrFromJson(const char *text)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(QByteArray(text), &err);
    REQUIRE(err.error == QJsonParseError::NoError);
    return doc.array();
}

// parseOSJson re-encodes each entry's subitems as a JSON *string* under
// "subitems_json", which is the form the QML model consumes. Decode it back
// so a test can talk about the images rather than the encoding.
QJsonArray subitemsOf(const QJsonValue &entry)
{
    const QString encoded = entry.toObject()["subitems_json"].toString();
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(encoded.toUtf8(), &err);
    REQUIRE(err.error == QJsonParseError::NoError);
    return doc.array();
}

QStringList namesOf(const QJsonArray &list)
{
    QStringList names;
    for (const auto &v : list)
        names << v.toObject()["name"].toString();
    return names;
}

} // namespace

// ══════════════════════════════════════════════════════════════
// init_format filtering
// ══════════════════════════════════════════════════════════════

TEST_CASE("An entry with no init_format is kept", "[oslist]")
{
    // Most entries in the real list have no init_format at all. If the empty
    // string ever stopped counting as valid, this filter would silently
    // delete almost the entire OS list and the chooser would come up bare.
    const auto list = arrFromJson(R"([
        {"name": "Raspberry Pi OS"},
        {"name": "Ubuntu Desktop"}
    ])");

    const auto filtered = oslist::filterInvalidInitFormats(list);
    CHECK(namesOf(filtered) == QStringList{"Raspberry Pi OS", "Ubuntu Desktop"});
}

TEST_CASE("Every documented init_format is accepted", "[oslist]")
{
    for (const char *fmt : {"", "systemd", "cloudinit", "cloudinit-rpi",
                            "rpi-preseed", "none"}) {
        INFO("init_format: '" << fmt << "'");
        CHECK(oslist::isValidInitFormat(QString::fromLatin1(fmt)));
    }
}

TEST_CASE("An unknown init_format is pruned", "[oslist]")
{
    // Offering an entry whose customisation format we cannot honour means the
    // user fills in a wifi password and hostname that are then dropped.
    const auto list = arrFromJson(R"([
        {"name": "Good",  "init_format": "systemd"},
        {"name": "Bogus", "init_format": "sysvinit"},
        {"name": "Also good"}
    ])");

    const auto filtered = oslist::filterInvalidInitFormats(list);
    CHECK(namesOf(filtered) == QStringList{"Good", "Also good"});
}

TEST_CASE("init_format filtering descends into subitems", "[oslist]")
{
    // The real list nests: a category holds the actual images. A bad entry
    // one level down has to be pruned too.
    const auto list = arrFromJson(R"([
        {"name": "Category", "subitems": [
            {"name": "Keep", "init_format": "cloudinit"},
            {"name": "Drop", "init_format": "nonsense"}
        ]}
    ])");

    const auto filtered = oslist::filterInvalidInitFormats(list);
    REQUIRE(filtered.size() == 1);
    const auto subs = filtered[0].toObject()["subitems"].toArray();
    CHECK(namesOf(subs) == QStringList{"Keep"});
}

// ══════════════════════════════════════════════════════════════
// Locale selection
// ══════════════════════════════════════════════════════════════

TEST_CASE("An exact locale list wins", "[oslist]")
{
    const auto root = objFromJson(R"({
        "os_list":       [{"name": "generic"}],
        "os_list_de":    [{"name": "german"}],
        "os_list_de_DE": [{"name": "german-germany"}]
    })");

    CHECK(namesOf(oslist::getListForLocale(root, "de_DE"))
          == QStringList{"german-germany"});
}

TEST_CASE("A locale falls back to its language", "[oslist]")
{
    // de_AT has no list of its own, but the German one is a better answer
    // than the untranslated default.
    const auto root = objFromJson(R"({
        "os_list":    [{"name": "generic"}],
        "os_list_de": [{"name": "german"}]
    })");

    CHECK(namesOf(oslist::getListForLocale(root, "de_AT")) == QStringList{"german"});
}

TEST_CASE("An unknown locale falls back to the default list", "[oslist]")
{
    const auto root = objFromJson(R"({
        "os_list":    [{"name": "generic"}],
        "os_list_de": [{"name": "german"}]
    })");

    CHECK(namesOf(oslist::getListForLocale(root, "ja_JP")) == QStringList{"generic"});
}

TEST_CASE("A document with no list at all yields nothing", "[oslist]")
{
    CHECK(oslist::getListForLocale(objFromJson(R"({"something_else": 1})"), "en_GB").isEmpty());
}

TEST_CASE("An empty locale list falls through to the default", "[oslist]")
{
    // A present-but-empty translated list must not blank the chooser.
    const auto root = objFromJson(R"({
        "os_list":       [{"name": "generic"}],
        "os_list_fr_FR": []
    })");

    CHECK(namesOf(oslist::getListForLocale(root, "fr_FR")) == QStringList{"generic"});
}

// ══════════════════════════════════════════════════════════════
// Full parse
// ══════════════════════════════════════════════════════════════

TEST_CASE("Parsing a document with no os_list yields nothing", "[oslist]")
{
    CHECK(oslist::parseOSJson(objFromJson(R"({})")).isEmpty());
}

TEST_CASE("Parsing keeps order for a group that is not random", "[oslist]")
{
    const auto root = objFromJson(R"({
        "os_list": [{"name": "Category", "subitems": [
            {"name": "A"}, {"name": "B"}, {"name": "C"}
        ]}]
    })");

    const auto parsed = oslist::parseOSJson(root);
    REQUIRE(parsed.size() == 1);
    CHECK(namesOf(subitemsOf(parsed[0])) == QStringList{"A", "B", "C"});
}

TEST_CASE("A random group keeps every entry", "[oslist]")
{
    // The order is deliberately unpredictable, so the property worth checking
    // is that shuffling neither drops nor duplicates an image.
    const auto root = objFromJson(R"({
        "os_list": [{"name": "Category", "random": true, "subitems": [
            {"name": "A"}, {"name": "B"}, {"name": "C"}, {"name": "D"}
        ]}]
    })");

    const auto parsed = oslist::parseOSJson(root);
    REQUIRE(parsed.size() == 1);
    QStringList got = namesOf(subitemsOf(parsed[0]));
    got.sort();
    CHECK(got == QStringList{"A", "B", "C", "D"});
}

// ══════════════════════════════════════════════════════════════
// Architecture ordering
// ══════════════════════════════════════════════════════════════

TEST_CASE("Images for the attached architecture are offered first", "[oslist]")
{
    // Otherwise the 64-bit image for the board in front of the user can sit
    // below several that will not run on it.
    auto list = arrFromJson(R"([{"name": "Category", "subitems": [
        {"name": "armhf-1", "architecture": "armhf"},
        {"name": "arm64-1", "architecture": "arm64"},
        {"name": "armhf-2", "architecture": "armhf"},
        {"name": "arm64-2", "architecture": "arm64"}
    ]}])");

    oslist::applyArchitectureSorting(list, QStringLiteral("arm64"));

    // Preferred first, and the original order preserved within each group.
    CHECK(namesOf(list[0].toObject()["subitems"].toArray())
          == QStringList{"arm64-1", "arm64-2", "armhf-1", "armhf-2"});
}

TEST_CASE("Architecture sorting leaves an entry with no subitems alone", "[oslist]")
{
    auto list = arrFromJson(R"([{"name": "Standalone", "architecture": "armhf"}])");
    oslist::applyArchitectureSorting(list, QStringLiteral("arm64"));
    CHECK(namesOf(list) == QStringList{"Standalone"});
}

// ══════════════════════════════════════════════════════════════
// Icon sources
//
// These strings come from a downloaded document and are handed to the image
// provider, so the malformed ones are dropped rather than fetched.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A repository-relative icon path is rebased", "[oslist][icon]")
{
    CHECK(oslist::sanitizeIconSource("icons/raspios.png") == "../icons/raspios.png");
}

TEST_CASE("Well-formed remote icons are allowed", "[oslist][icon]")
{
    CHECK(oslist::sanitizeIconSource("https://downloads.raspberrypi.org/i.png")
          == "https://downloads.raspberrypi.org/i.png");
    CHECK(oslist::sanitizeIconSource("http://example.com/i.png")
          == "http://example.com/i.png");
}

TEST_CASE("A remote icon with no host is dropped", "[oslist][icon]")
{
    // "https:///icon.png" would otherwise become a fetch of nothing.
    CHECK(oslist::sanitizeIconSource("https:///icon.png").isEmpty());
}

TEST_CASE("A local file icon is allowed", "[oslist][icon]")
{
    // What --repo takes when it is pointed at a directory on disk: the icons
    // beside it are named as file URLs.
    CHECK(oslist::sanitizeIconSource("file:///home/user/icons/raspios.png")
          == "file:///home/user/icons/raspios.png");
    // The single-slash form, which carries no host either.
    CHECK(oslist::sanitizeIconSource("file:/home/user/icons/raspios.png")
          == "file:/home/user/icons/raspios.png");
}

TEST_CASE("A file icon naming a host is dropped", "[oslist][icon]")
{
    // The one this function exists for. A repository is not necessarily
    // trusted -- it can arrive from --repo, from the repository dialog, or
    // from an rpi-imager:// link somebody was persuaded to accept -- and
    // file://host/share/icon.png becomes the UNC path //host/share/icon.png.
    // On Windows an Image pointed at that reaches out to the host over SMB,
    // handing it an authentication attempt, for no reason the user could see
    // or refuse.
    //
    // Not caught by QUrl::isLocalFile(), which tests the scheme and nothing
    // else and answers true for all of these.
    CHECK(oslist::sanitizeIconSource("file://server/share/icon.png").isEmpty());
    CHECK(oslist::sanitizeIconSource("file://evil.example/x.png").isEmpty());
    // Qt gives "localhost" no special meaning here -- toLocalFile() leaves it
    // in the path -- so neither does this.
    CHECK(oslist::sanitizeIconSource("file://localhost/home/user/i.png").isEmpty());
}

TEST_CASE("A bundled icon is allowed", "[oslist][icon]")
{
    CHECK(oslist::sanitizeIconSource("qrc:/icons/raspios.png")
          == "qrc:/icons/raspios.png");
    CHECK(oslist::sanitizeIconSource("qrc://icons/raspios.png")
          == "qrc://icons/raspios.png");
}

TEST_CASE("An icon with an unrecognised scheme is passed through", "[oslist][icon]")
{
    // Deliberate rather than an oversight: QML may know a scheme this does
    // not, and the alternative is dropping icons that would have worked. It
    // is pinned so the choice is visible to whoever reads the coverage.
    CHECK(oslist::sanitizeIconSource("image://icons/https://example.com/i.png")
          == "image://icons/https://example.com/i.png");
}

TEST_CASE("An empty icon stays empty", "[oslist][icon]")
{
    CHECK(oslist::sanitizeIconSource(QString()).isEmpty());
    CHECK(oslist::sanitizeIconSource("").isEmpty());
}

TEST_CASE("qrc and local file icons are allowed", "[oslist][icon]")
{
    CHECK(oslist::sanitizeIconSource("qrc:/icons/ubuntu.png") == "qrc:/icons/ubuntu.png");
    CHECK(oslist::sanitizeIconSource("file:///usr/share/icons/x.png")
          == "file:///usr/share/icons/x.png");
}

TEST_CASE("A bare relative icon path is left for QML to resolve", "[oslist][icon]")
{
    CHECK(oslist::sanitizeIconSource("images/x.png") == "images/x.png");
}

TEST_CASE("Subitems are handed to the model as an encoded string", "[oslist]")
{
    // The QML model reads "subitems_json", not "subitems". Emitting the wrong
    // one leaves every category in the chooser looking empty.
    const auto root = objFromJson(R"({
        "os_list": [{"name": "Category", "subitems": [{"name": "A"}]}]
    })");

    const auto parsed = oslist::parseOSJson(root);
    REQUIRE(parsed.size() == 1);
    const auto entry = parsed[0].toObject();

    CHECK(entry.contains("subitems_json"));
    CHECK_FALSE(entry.contains("subitems"));
    CHECK(namesOf(subitemsOf(parsed[0])) == QStringList{"A"});
}

TEST_CASE("An entry with no subitems gains no encoded key", "[oslist]")
{
    const auto root = objFromJson(R"({"os_list": [{"name": "Standalone"}]})");
    const auto parsed = oslist::parseOSJson(root);
    REQUIRE(parsed.size() == 1);
    CHECK_FALSE(parsed[0].toObject().contains("subitems_json"));
}
