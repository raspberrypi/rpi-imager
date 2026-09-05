/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * ImPasswordField: the control the user's Wi-Fi password is typed into.
 *
 * Two ways this hurts a real person. Reveal the password when they did not
 * ask -- over their shoulder, on a shared screen, in a screen recording of
 * the imaging step. Or refuse to reveal it when they did ask, and they
 * cannot check the password they just typed before writing it to a card
 * they are about to put in a headless machine.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImPasswordField"
    when: windowShown
    width: 400
    height: 120
    visible: true

    Component {
        id: fieldComponent
        ImPasswordField {
            width: 300
            height: 40
        }
    }

    function create(props) {
        const field = createTemporaryObject(fieldComponent, testCase, props)
        verify(field, "the password field was created")
        return field
    }

    // -- The reveal toggle appears only once there is something to reveal --

    function test_empty_field_offers_no_toggle() {
        const field = create({})
        compare(field.text, "")
        verify(!field.eyeToggleVisible,
               "an empty field has nothing worth revealing")
    }

    function test_typing_brings_up_the_toggle() {
        const field = create({})
        field.textField.forceActiveFocus()
        verify(field.textField.activeFocus)

        keyClick(Qt.Key_S)
        compare(field.text, "s")
        verify(field.eyeToggleVisible,
               "once there is a password there is something to reveal")
    }

    function test_clearing_the_field_takes_the_toggle_away() {
        const field = create({ text: "hunter2" })
        verify(field.eyeToggleVisible)

        field.text = ""
        verify(!field.eyeToggleVisible)
    }

    function test_a_password_restored_from_settings_offers_no_toggle() {
        // The field is told not to offer the toggle when the password came
        // from saved settings rather than from the person at the keyboard.
        const field = create({ text: "", showToggle: false })
        verify(!field.eyeToggleVisible)

        field.text = "restored"
        verify(!field.eyeToggleVisible,
               "showToggle: false wins over there being text")
    }

    // -- Revealing, and going back -----------------------------------------

    function test_a_password_starts_hidden() {
        const field = create({ text: "hunter2" })
        verify(!field.passwordVisible)
        compare(field.textField.echoMode, TextInput.Password)
    }

    function test_the_toggle_reveals_and_hides_again() {
        const field = create({ text: "hunter2" })

        field.passwordVisible = true
        compare(field.textField.echoMode, TextInput.Normal,
                "revealing shows the characters")

        field.passwordVisible = false
        compare(field.textField.echoMode, TextInput.Password,
                "and hiding puts them back")
    }

    function test_f2_toggles_the_reveal() {
        const field = create({ text: "hunter2" })
        field.textField.forceActiveFocus()
        verify(field.textField.activeFocus)

        keyClick(Qt.Key_F2)
        verify(field.passwordVisible, "F2 reveals")

        keyClick(Qt.Key_F2)
        verify(!field.passwordVisible, "F2 again hides")
    }

    function test_f2_does_nothing_when_there_is_no_toggle() {
        // Consistency between the shortcut and the button: if the eye is not
        // being offered, the shortcut behind it must not work either.
        const field = create({ text: "hunter2", showToggle: false })
        field.textField.forceActiveFocus()

        keyClick(Qt.Key_F2)
        verify(!field.passwordVisible,
               "the shortcut is not a way around showToggle: false")
        compare(field.textField.echoMode, TextInput.Password)
    }

    function test_f2_on_an_empty_field_does_nothing() {
        const field = create({})
        field.textField.forceActiveFocus()

        keyClick(Qt.Key_F2)
        verify(!field.passwordVisible)
    }

    // -- Focus -------------------------------------------------------------

    function test_focusing_the_wrapper_lands_on_the_text_field() {
        // The outer Item is what tab order sees; the caret has to end up in
        // the field, not on the wrapper, or the next keystroke goes nowhere.
        const field = create({})
        field.forceActiveFocus()
        verify(field.textFieldHasFocus,
               "focus is delegated to the inner field")
    }

    function test_typing_after_focusing_the_wrapper_reaches_the_field() {
        const field = create({})
        field.forceActiveFocus()

        keyClick(Qt.Key_A)
        keyClick(Qt.Key_B)
        compare(field.text, "ab")
    }

    // -- What the caller reads back ----------------------------------------

    function test_surrounding_spaces_are_kept() {
        // A space is a legitimate character in a Wi-Fi password, so unlike
        // ImTextField this control must not trim. Silently dropping one
        // gives a card that cannot join the network, with nothing on screen
        // to explain why.
        const field = create({ text: "  spaced  " })
        compare(field.value, "  spaced  ")
    }

    function test_control_characters_never_reach_the_value() {
        // Pasting out of a password manager can bring a trailing newline
        // with it. A space is not a control character and stays.
        const field = create({})
        field.text = "pass word" + String.fromCharCode(10)
        compare(field.text, "pass word", "scrubbed out of the text itself")
        compare(field.value, "pass word", "and out of what the caller reads")
    }
}
