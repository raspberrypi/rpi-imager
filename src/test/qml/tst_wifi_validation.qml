/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * WPA2/WPA3 pre-shared key validation on the Wi-Fi step.
 *
 * This one has no second chance. The card goes into a machine with no
 * screen and no keyboard, and the only sign that the password was wrong is
 * that the Pi never appears on the network -- with nothing to distinguish
 * "wrong password" from "wrong SSID", "out of range" or "did not boot".
 *
 * So both directions cost the user real time. Reject something valid and
 * they cannot get past the step at all. Accept something invalid and they
 * find out after writing the card, booting the machine, and going looking
 * for it.
 *
 * The rule, from the standard: a passphrase is 8 to 63 printable ASCII
 * characters, or the raw key as exactly 64 hex digits.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "WifiValidation"
    when: windowShown
    width: 500
    height: 300
    visible: true

    // The step needs a container. Only these fields are read on the paths
    // exercised here; the validators themselves touch none of them.
    QtObject {
        id: settingsStub
        property string wifiSSID: ""
        property string wifiMode: "secure"
        property string wifiPasswordCrypt: ""
        property bool wifiHidden: false
        property int wifiSsidOctetsBase: 0
    }

    QtObject {
        id: containerStub
        // WizardStepBase reads this on every step.
        property string networkInfoText: ""
        property var customizationSettings: settingsStub
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property int stepWriting: 0
        function jumpToStep(i) {}
    }

    Component {
        id: stepComponent
        WifiCustomizationStep { wizardContainer: containerStub }
    }

    // createObject rather than createTemporaryObject: the latter is destroyed
    // at the end of each test function, which leaves this null everywhere
    // when it is built once in initTestCase.
    property var step: null

    function initTestCase() {
        step = stepComponent.createObject(testCase)
        verify(step, "the Wi-Fi step was created")
    }

    function cleanupTestCase() {
        if (step) {
            step.destroy()
            step = null
        }
    }

    function repeated(ch, n) {
        let s = ""
        for (let i = 0; i < n; i++) s += ch
        return s
    }

    // -- Printable ASCII ---------------------------------------------------

    function test_ordinary_characters_are_printable() {
        verify(step.isAsciiPrintable("password"))
        verify(step.isAsciiPrintable("P@ssw0rd!"))
        verify(step.isAsciiPrintable("with spaces in it"))
    }

    function test_the_edges_of_the_printable_range_are_included() {
        // 32 is space and 126 is ~. Both are legal in a passphrase, and a
        // trailing space is a real thing people set.
        verify(step.isAsciiPrintable(String.fromCharCode(32)))
        verify(step.isAsciiPrintable(String.fromCharCode(126)))
    }

    function test_control_characters_are_not_printable() {
        verify(!step.isAsciiPrintable("pass" + String.fromCharCode(9) + "word"),
               "tab")
        verify(!step.isAsciiPrintable("pass" + String.fromCharCode(10)),
               "newline")
        verify(!step.isAsciiPrintable(String.fromCharCode(0)), "nul")
        verify(!step.isAsciiPrintable(String.fromCharCode(127)), "delete")
    }

    function test_characters_beyond_ascii_are_not_printable() {
        // The standard says ASCII. A passphrase with an accent in it would
        // be encoded differently by the router than by us, and the failure
        // arrives on a machine with no screen.
        verify(!step.isAsciiPrintable("café"))
        verify(!step.isAsciiPrintable("пароль"))
        verify(!step.isAsciiPrintable("password😀"))
    }

    function test_an_empty_string_is_vacuously_printable() {
        verify(step.isAsciiPrintable(""))
    }

    // -- The raw 64-digit key ----------------------------------------------

    function test_sixty_four_hex_digits_is_a_raw_key() {
        verify(step.isHex64(repeated("a", 64)))
        verify(step.isHex64(repeated("0", 64)))
        verify(step.isHex64(repeated("F", 64)), "upper case too")
        verify(step.isHex64(repeated("aB3f", 16)), "mixed")
    }

    function test_the_wrong_length_is_not_a_raw_key() {
        verify(!step.isHex64(repeated("a", 63)))
        verify(!step.isHex64(repeated("a", 65)))
        verify(!step.isHex64(""))
    }

    function test_non_hex_characters_are_not_a_raw_key() {
        verify(!step.isHex64(repeated("g", 64)), "g is past f")
        verify(!step.isHex64(repeated("a", 63) + "z"), "one bad digit at the end")
        verify(!step.isHex64("z" + repeated("a", 63)), "one bad digit at the start")
        verify(!step.isHex64(repeated("a", 32) + " " + repeated("a", 31)),
               "a space in the middle")
    }

    // -- The rule the user meets -------------------------------------------

    function test_an_empty_password_is_allowed() {
        // Open networks, and the case where a saved key is being kept.
        verify(step.isValidWifiPassword(""))
    }

    function test_a_passphrase_of_eight_characters_is_allowed() {
        // Exactly the floor. Off by one here and the shortest legal
        // password in the world is rejected.
        verify(step.isValidWifiPassword(repeated("a", 8)))
    }

    function test_a_passphrase_of_sixty_three_characters_is_allowed() {
        verify(step.isValidWifiPassword(repeated("a", 63)), "exactly the ceiling")
    }

    function test_a_passphrase_shorter_than_eight_is_rejected() {
        verify(!step.isValidWifiPassword(repeated("a", 7)))
        verify(!step.isValidWifiPassword("a"))
    }

    function test_a_passphrase_of_sixty_four_characters_is_rejected_unless_hex() {
        // 64 is the raw-key length, not a passphrase length. A 64-character
        // passphrase that happens not to be hex is not valid either way.
        verify(!step.isValidWifiPassword(repeated("z", 64)))
        verify(step.isValidWifiPassword(repeated("a", 64)), "but 64 hex is")
    }

    function test_a_passphrase_longer_than_sixty_four_is_rejected() {
        verify(!step.isValidWifiPassword(repeated("a", 65)))
    }

    function test_a_passphrase_with_spaces_is_allowed() {
        verify(step.isValidWifiPassword("correct horse battery"))
    }

    function test_a_passphrase_with_punctuation_is_allowed() {
        verify(step.isValidWifiPassword("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"))
    }

    function test_a_passphrase_with_a_control_character_is_rejected() {
        // What a paste out of a password manager brings with it.
        verify(!step.isValidWifiPassword("password" + String.fromCharCode(10)))
    }

    function test_a_passphrase_with_an_accent_is_rejected() {
        verify(!step.isValidWifiPassword("café-wireless"))
    }

    function test_a_long_enough_non_ascii_passphrase_is_still_rejected() {
        // Length alone is not enough; the character check applies over the
        // whole string rather than a prefix.
        verify(!step.isValidWifiPassword(repeated("a", 30) + "é" + repeated("a", 30)))
    }
}
