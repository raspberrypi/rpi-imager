/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Merging a customisation setting into config.txt.
 *
 * Both write paths do this -- the SD card one in DownloadThread and the
 * compute module one in FastbootFlashThread -- and both used to ask whether
 * the file contained "\n" + item or "#" + item as a substring. config.txt
 * is full of settings that share a prefix with a longer one, so that
 * question has the wrong answer more often than it looks.
 */

#include <catch2/catch_test_macros.hpp>

#include "config_txt_merge.h"

namespace {

QByteArray merge(QByteArray config, const QList<QByteArray> &items)
{
    for (const QByteArray &item : items)
        config = mergeConfigTxtItem(config, item);
    return config;
}

// Whether `config` contains `line` as a line of its own.
bool hasLine(const QByteArray &config, const QByteArray &line)
{
    for (const QByteArray &l : config.split('\n')) {
        const QByteArray s = l.endsWith('\r') ? l.left(l.size() - 1) : l;
        if (s == line)
            return true;
    }
    return false;
}

} // namespace

// ── The prefix collisions ───────────────────────────────────────────────

TEST_CASE("A setting is added even when a longer one shares its prefix",
          "[configtxt]")
{
    // vc4-kms-v3d and vc4-kms-v3d-pi5 are both real overlays. Asking for the
    // first on a card whose config already names the second used to be taken
    // as already satisfied, and the request was dropped.
    const QByteArray before = "[all]\ndtoverlay=vc4-kms-v3d-pi5\n";
    const QByteArray after = merge(before, {"dtoverlay=vc4-kms-v3d"});

    INFO("after:\n" << after.toStdString());
    CHECK(hasLine(after, "dtoverlay=vc4-kms-v3d"));
    CHECK(hasLine(after, "dtoverlay=vc4-kms-v3d-pi5"));
}

TEST_CASE("A commented longer setting is not uncommented in its place",
          "[configtxt]")
{
    // The worse half: the substring matched the commented -pi5 line, so that
    // line was uncommented and an overlay the user never asked for was
    // enabled, while the one they did ask for was still missing.
    const QByteArray before = "[all]\n#dtoverlay=vc4-kms-v3d-pi5\n";
    const QByteArray after = merge(before, {"dtoverlay=vc4-kms-v3d"});

    INFO("after:\n" << after.toStdString());
    CHECK(hasLine(after, "dtoverlay=vc4-kms-v3d"));
    CHECK_FALSE(hasLine(after, "dtoverlay=vc4-kms-v3d-pi5"));
    CHECK(hasLine(after, "#dtoverlay=vc4-kms-v3d-pi5"));
}

TEST_CASE("A setting whose prefix is a shorter existing one is added",
          "[configtxt]")
{
    // The other way round: asking for the longer one when the shorter is
    // present. Substring matching got this right by luck; whole-line
    // matching gets it right on purpose.
    const QByteArray before = "[all]\ndtoverlay=vc4-kms-v3d\n";
    const QByteArray after = merge(before, {"dtoverlay=vc4-kms-v3d-pi5"});

    CHECK(hasLine(after, "dtoverlay=vc4-kms-v3d"));
    CHECK(hasLine(after, "dtoverlay=vc4-kms-v3d-pi5"));
}

// ── The three outcomes ──────────────────────────────────────────────────

TEST_CASE("A setting already present is left alone", "[configtxt]")
{
    const QByteArray before = "[all]\ndtparam=audio=on\n";
    CHECK(merge(before, {"dtparam=audio=on"}) == before);
}

TEST_CASE("A commented setting is uncommented where it stands", "[configtxt]")
{
    // In place, not appended: config.txt is sectioned, and a setting that
    // moves out of its [section] applies to the wrong hardware.
    const QByteArray before = "[all]\n#dtparam=audio=on\n[pi5]\narm_boost=1\n";
    const QByteArray after = merge(before, {"dtparam=audio=on"});

    CHECK(after == "[all]\ndtparam=audio=on\n[pi5]\narm_boost=1\n");
}

TEST_CASE("A setting that is not there is appended", "[configtxt]")
{
    const QByteArray after = merge("[all]\n", {"dtparam=i2c_arm=on"});
    CHECK(after == "[all]\ndtparam=i2c_arm=on\n");
}

TEST_CASE("A file with no trailing newline gains one", "[configtxt]")
{
    // Otherwise the appended setting joins the last line and neither parses.
    const QByteArray after = merge("[all]\narm_64bit=1", {"dtparam=audio=on"});
    CHECK(after == "[all]\narm_64bit=1\ndtparam=audio=on\n");
}

TEST_CASE("An empty config gets just the setting", "[configtxt]")
{
    CHECK(merge(QByteArray(), {"arm_64bit=1"}) == "arm_64bit=1\n");
}

TEST_CASE("An empty setting changes nothing", "[configtxt]")
{
    const QByteArray before = "[all]\narm_64bit=1\n";
    CHECK(merge(before, {QByteArray()}) == before);
}

// ── Several at once, and the shape of the file ──────────────────────────

TEST_CASE("Several settings all arrive", "[configtxt]")
{
    const QByteArray after = merge("[all]\n", {
        "dtparam=audio=on", "dtparam=i2c_arm=on", "dtoverlay=w1-gpio",
    });

    CHECK(hasLine(after, "dtparam=audio=on"));
    CHECK(hasLine(after, "dtparam=i2c_arm=on"));
    CHECK(hasLine(after, "dtoverlay=w1-gpio"));
}

TEST_CASE("Applying the same setting twice adds it once", "[configtxt]")
{
    const QByteArray after = merge("[all]\n", {"dtparam=audio=on", "dtparam=audio=on"});

    int count = 0;
    for (const QByteArray &l : after.split('\n'))
        if (l == "dtparam=audio=on")
            ++count;
    CHECK(count == 1);
}

TEST_CASE("The rest of the file is left as it was", "[configtxt]")
{
    // Comments and sections a user put there are theirs, not ours to tidy.
    const QByteArray before =
        "# My settings\n"
        "[all]\n"
        "arm_64bit=1\n"
        "\n"
        "# leave this alone\n"
        "[pi4]\n"
        "over_voltage=2\n";
    const QByteArray after = merge(before, {"dtparam=audio=on"});

    CHECK(after.startsWith(before));
    CHECK(hasLine(after, "# leave this alone"));
    CHECK(hasLine(after, "over_voltage=2"));
}

TEST_CASE("A file with CRLF endings keeps them", "[configtxt]")
{
    // config.txt lives on a FAT partition and may have been edited on
    // Windows. A line matched against its CR would never be recognised, and
    // the setting would be added a second time.
    const QByteArray before = "[all]\r\n#dtparam=audio=on\r\n";
    const QByteArray after = merge(before, {"dtparam=audio=on"});

    INFO("after: " << after.toStdString());
    CHECK(hasLine(after, "dtparam=audio=on"));
    CHECK_FALSE(hasLine(after, "#dtparam=audio=on"));
    CHECK(after.contains("\r\n"));
}

TEST_CASE("A CRLF file does not gain a duplicate", "[configtxt]")
{
    const QByteArray before = "[all]\r\ndtparam=audio=on\r\n";
    CHECK(merge(before, {"dtparam=audio=on"}) == before);
}

TEST_CASE("Only the first commented copy is uncommented", "[configtxt]")
{
    // A config that names the same setting twice, both commented. Turning on
    // both would be a change the user did not ask for.
    const QByteArray before = "[all]\n#dtparam=audio=on\n[pi5]\n#dtparam=audio=on\n";
    const QByteArray after = merge(before, {"dtparam=audio=on"});

    int live = 0, commented = 0;
    for (const QByteArray &l : after.split('\n')) {
        if (l == "dtparam=audio=on") ++live;
        if (l == "#dtparam=audio=on") ++commented;
    }
    CHECK(live == 1);
    CHECK(commented == 1);
}

// ── Round-tripping the file ─────────────────────────────────────────────
//
// The merge splits on newlines and rejoins. Anything that survives an
// unrelated edit has to come back byte for byte, or a customisation run
// quietly reformats a file the user wrote.

TEST_CASE("Uncommenting does not add a trailing newline that was not there",
          "[configtxt]")
{
    // A config.txt with no final newline is unusual but legal, and it is
    // not the customiser's business to change it.
    const QByteArray before = "[all]\n#dtparam=audio=on";
    const QByteArray after = merge(before, {"dtparam=audio=on"});

    CHECK(after == "[all]\ndtparam=audio=on");
    CHECK_FALSE(after.endsWith('\n'));
}

TEST_CASE("A setting already present leaves the file byte for byte",
          "[configtxt]")
{
    // Not merely equivalent -- identical. The file is returned untouched
    // rather than rebuilt.
    const QByteArray before = "[all]\r\n\r\ndtparam=audio=on\r\n\r\n# trailing\r\n";
    CHECK(merge(before, {"dtparam=audio=on"}) == before);
}

TEST_CASE("Blank lines and spacing survive an append", "[configtxt]")
{
    const QByteArray before = "[all]\n\n\narm_64bit=1\n\n";
    const QByteArray after = merge(before, {"dtparam=audio=on"});

    CHECK(after == "[all]\n\n\narm_64bit=1\n\ndtparam=audio=on\n");
}

TEST_CASE("A setting is not matched inside a longer line", "[configtxt]")
{
    // "dtparam=audio=on" appears within this line but is not this line.
    // Substring matching treated it as present; whole-line matching does not.
    const QByteArray before = "[all]\n# see also dtparam=audio=on for sound\n";
    const QByteArray after = merge(before, {"dtparam=audio=on"});

    CHECK(hasLine(after, "dtparam=audio=on"));
    CHECK(hasLine(after, "# see also dtparam=audio=on for sound"));
}
