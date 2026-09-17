/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Which letter the boot image is mounted on.
 *
 * createBootImg attaches the image as a virtual disk, has diskpart give it a
 * drive letter, and writes the boot files to that letter. The letter used to
 * be Z, unconditionally.
 *
 * Z is a popular choice for a mapped network drive, and diskpart reports a
 * failed `assign` in its output while still exiting zero. So where Z was
 * already taken: the assign failed, the exit code said otherwise, and the boot
 * files were written to whatever Z already was. Somebody's share gained a
 * config.txt, and the image came back formatted, empty and reported as a
 * success -- an unbootable card with nothing on screen saying why.
 *
 * The rest of createBootImg needs elevation and a virtual disk. These two
 * decisions need neither, and they are the two that decided where the files
 * went.
 */

#include <catch2/catch_test_macros.hpp>

namespace BootImgCreatorTesting {
char chooseFreeDriveLetter(unsigned long mask);
bool letterPresentIn(unsigned long mask, char letter);
}

namespace {

// The bit GetLogicalDrives() sets for a letter.
constexpr unsigned long bit(char letter)
{
    return 1ul << (letter - 'A');
}

} // namespace

using BootImgCreatorTesting::chooseFreeDriveLetter;
using BootImgCreatorTesting::letterPresentIn;

TEST_CASE("The highest free letter is chosen", "[bootimg][driveletter]")
{
    // C and D in use, as an ordinary machine has.
    CHECK(chooseFreeDriveLetter(bit('C') | bit('D')) == 'Z');
}

TEST_CASE("A letter already in use is not chosen", "[bootimg][driveletter]")
{
    // The case the old code got wrong: Z is a mapped drive.
    CHECK(chooseFreeDriveLetter(bit('C') | bit('Z')) == 'Y');
    // And several taken from the top.
    CHECK(chooseFreeDriveLetter(bit('C') | bit('Z') | bit('Y') | bit('X')) == 'W');
}

TEST_CASE("Only one letter left is still found", "[bootimg][driveletter]")
{
    // Everything from E to Z taken, leaving D.
    unsigned long mask = 0;
    for (char c = 'E'; c <= 'Z'; ++c)
        mask |= bit(c);
    CHECK(chooseFreeDriveLetter(mask) == 'D');
}

TEST_CASE("No free letter is reported rather than guessed at",
          "[bootimg][driveletter]")
{
    // Nought, so the caller refuses. Returning some letter anyway is how the
    // files ended up somewhere they did not belong.
    unsigned long mask = 0;
    for (char c = 'A'; c <= 'Z'; ++c)
        mask |= bit(c);
    CHECK(chooseFreeDriveLetter(mask) == 0);
}

TEST_CASE("The floppy letters are never chosen", "[bootimg][driveletter]")
{
    // A and B are free on every machine made this century, and Windows still
    // treats them differently. Handing one out would be a surprise.
    unsigned long mask = 0;
    for (char c = 'C'; c <= 'Z'; ++c)
        mask |= bit(c);
    CHECK(chooseFreeDriveLetter(mask) == 0);
}

TEST_CASE("A letter that appeared is seen", "[bootimg][driveletter]")
{
    CHECK(letterPresentIn(bit('C') | bit('Y'), 'Y'));
    CHECK(letterPresentIn(bit('C'), 'C'));
}

TEST_CASE("A letter that never appeared is not claimed",
          "[bootimg][driveletter]")
{
    // What the check after the assign is for: diskpart exits zero having
    // failed, and writing to a letter this image did not get writes into
    // whatever else holds it.
    CHECK_FALSE(letterPresentIn(bit('C'), 'Y'));
    CHECK_FALSE(letterPresentIn(0, 'Z'));
}

TEST_CASE("A letter outside the alphabet is refused, not shifted",
          "[bootimg][driveletter]")
{
    // Nought is what choosing answers when it finds nothing, and it reaches
    // here if a caller passes it on. Shifting by a negative is undefined, and
    // would answer from whatever bit it landed on.
    CHECK_FALSE(letterPresentIn(0xFFFFFFFF, 0));
    CHECK_FALSE(letterPresentIn(0xFFFFFFFF, '0'));
    CHECK_FALSE(letterPresentIn(0xFFFFFFFF, 'a'));
}
