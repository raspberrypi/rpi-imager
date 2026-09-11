/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The last thing standing between a user and a fuse they cannot un-blow.
 *
 * Programming the secure boot key into a Pi's OTP memory is permanent. There
 * is no undo, no reflash, no support call that recovers the board: the wrong
 * key in OTP means that device will only ever boot images signed with it, and
 * the wrong device means somebody else's board is now locked to a key they do
 * not have.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "OtpConfirmation"
    when: windowShown
    width: 700
    height: 620
    visible: true

    readonly property string serial: "10000000abcd1234"

    Component {
        id: dialogComponent
        ConfirmOtpProgramDialog {
            overlayParent: testCase
            deviceSerial: testCase.serial
            deviceModel: "Raspberry Pi 5 Model B"
            keyFingerprint: "SHA256:5f2c1a9e"
        }
    }

    property var dialog: null
    property int confirmedCount: 0
    property int cancelledCount: 0

    function init() {
        confirmedCount = 0
        cancelledCount = 0
        dialog = dialogComponent.createObject(testCase)
        verify(dialog, "the dialog was created")
        dialog.confirmed.connect(function () { testCase.confirmedCount++ })
        dialog.cancelled.connect(function () { testCase.cancelledCount++ })
        dialog.open()
        tryVerify(function () { return dialog.opened }, 3000, "the dialog opened")
    }

    function cleanup() {
        if (dialog) {
            dialog.close()
            dialog.destroy()
            dialog = null
        }
    }

    function child(name) {
        var c = findChild(dialog, name)
        verify(c, "found " + name)
        return c
    }

    function test_the_button_is_dead_until_the_serial_is_typed_exactly() {
        var input = child("otpConfirmInput")
        var program = child("otpProgramButton")

        verify(!program.enabled, "nothing typed, so nothing to program")

        input.text = "10000000"
        verify(!program.enabled, "a prefix of the serial is not the serial")

        input.text = "10000000abcd1235"
        verify(!program.enabled, "one digit out is a different board")

        input.text = testCase.serial.toUpperCase()
        verify(!program.enabled,
               "the comparison is exact -- the serial is shown in lower case")

        input.text = testCase.serial + " "
        verify(!program.enabled, "a trailing space is not trimmed away")

        input.text = testCase.serial
        verify(program.enabled, "typed exactly, so the button comes alive")
    }

    function test_a_device_with_no_serial_cannot_be_programmed() {
        // Reached where the serial could not be read off the device. An empty
        // input would otherwise equal an empty serial and the button would be
        // live with nothing typed at all -- one click away from programming a
        // board nobody has identified.
        dialog.deviceSerial = ""
        var input = child("otpConfirmInput")
        var program = child("otpProgramButton")

        input.text = ""
        verify(!program.enabled, "no serial to confirm against")

        input.text = " "
        verify(!program.enabled)
    }

    function test_the_serial_cannot_be_pasted() {
        // The point of typing it is that the user reads it off the device and
        // off the screen and finds they agree. Pasting turns that into two
        // keystrokes and confirms nothing.
        var input = child("otpConfirmInput")
        var program = child("otpProgramButton")

        ClipboardHelper.setText(testCase.serial)
        compare(ClipboardHelper.getText(), testCase.serial,
                "the clipboard really does hold the serial")

        input.forceActiveFocus()
        keyClick(Qt.Key_V, Qt.ControlModifier)
        compare(input.text, "", "Ctrl+V was refused")
        verify(!program.enabled)

        keyClick(Qt.Key_Insert, Qt.ShiftModifier)
        compare(input.text, "", "Shift+Insert was refused too")
        verify(!program.enabled)
    }

    function test_typing_still_works() {
        // The paste block must not have taken ordinary typing with it.
        var input = child("otpConfirmInput")
        input.forceActiveFocus()
        keyClick(Qt.Key_1)
        keyClick(Qt.Key_0)
        compare(input.text, "10", "keys reach the field")
    }

    function test_reopening_starts_from_empty() {
        // Somebody types the serial, thinks better of it, cancels, and comes
        // back. If the text were still there the button would be live before
        // they had looked at the dialog.
        var input = child("otpConfirmInput")
        input.text = testCase.serial
        verify(child("otpProgramButton").enabled)

        dialog.close()
        tryVerify(function () { return !dialog.opened }, 3000)
        dialog.open()
        tryVerify(function () { return dialog.opened }, 3000)

        compare(child("otpConfirmInput").text, "", "the typed serial is gone")
        verify(!child("otpProgramButton").enabled)
    }

    function test_cancel_cancels() {
        var input = child("otpConfirmInput")
        input.text = testCase.serial
        verify(child("otpProgramButton").enabled,
               "armed, so this is a cancel of something that would have gone ahead")

        child("otpCancelButton").clicked()

        compare(testCase.confirmedCount, 0, "nothing was programmed")
        compare(testCase.cancelledCount, 1)
        tryVerify(function () { return !dialog.opened }, 3000, "the dialog closed")
    }

    function test_escape_cancels() {
        // The reflex when a red warning appears. It has to mean no, and it
        // has to say so to the caller rather than just closing the window and
        // leaving whatever asked for this waiting.
        var input = child("otpConfirmInput")
        input.text = testCase.serial

        dialog.escapePressed()

        compare(testCase.confirmedCount, 0)
        compare(testCase.cancelledCount, 1)
        tryVerify(function () { return !dialog.opened }, 3000)
    }

    function test_confirming_reports_it_once() {
        var input = child("otpConfirmInput")
        input.text = testCase.serial

        child("otpProgramButton").clicked()

        compare(testCase.confirmedCount, 1)
        compare(testCase.cancelledCount, 0, "confirming is not also a cancel")
        tryVerify(function () { return !dialog.opened }, 3000)
    }

    function test_enter_on_the_input_does_nothing_until_it_matches() {
        // Enter is a shortcut for the button, and inherits the button's
        // guard: a half-typed serial and a stab at Enter must not program
        // anything.
        var input = child("otpConfirmInput")
        input.forceActiveFocus()
        input.text = "10000000"
        keyClick(Qt.Key_Return)
        compare(testCase.confirmedCount, 0, "not armed, so Enter did nothing")

        input.text = testCase.serial
        keyClick(Qt.Key_Return)
        compare(testCase.confirmedCount, 1)
    }
}
