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

    // -- Tab navigation reaching the inner field ---------------------------
    //
    // Focus lives on the inner ImTextField while KeyNavigation is set on the
    // wrapper by whatever lays the step out, so the wrapper's tab targets
    // have to be copied inward. There are two syncs: one on completion, and
    // one when the field takes focus, for the case where the step wired the
    // ring after the field was built.

    function test_tab_targets_reach_the_inner_field_on_construction() {
        const field = create({})
        const other = create({})
        field.KeyNavigation.tab = other

        // The completion sync is deferred, so give it the event loop.
        tryCompare(field.textField.KeyNavigation, "tab", other)
    }

    function test_tab_targets_set_after_construction_still_reach_it() {
        // The case the focus-time sync exists for: the step assigns the ring
        // once every control is built, which is after this field's own
        // completion handler has already run.
        const field = create({})
        const other = create({})
        // Long enough to flush the deferred completion sync, so what is
        // asserted below is the focus-time one and not that arriving late.
        wait(100)
        verify(!field.textField.KeyNavigation.tab,
               "nothing has been copied inward yet")

        field.KeyNavigation.tab = other
        field.textField.forceActiveFocus()
        verify(field.textField.activeFocus)

        // 4th arg is the timeout; the message goes after it.
        tryCompare(field.textField.KeyNavigation, "tab", other, 2000,
                   "taking focus re-reads the wrapper's tab target")
    }

    function test_backtab_is_carried_inward_too() {
        const field = create({})
        const previous = create({})
        wait(100)

        field.KeyNavigation.backtab = previous
        field.textField.forceActiveFocus()

        tryCompare(field.textField.KeyNavigation, "backtab", previous)
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


    // -- Pressing the eye, rather than setting the property ----------------
    //
    // The cases above set passwordVisible, or use the F2 shortcut. The
    // button's own handler was uncovered, and it does one thing more than
    // flip the property: it puts focus back in the text field.
    //
    // Without that the caret leaves the password when the user checks what
    // they typed, and the next character they type goes to the button
    // instead of the field. They see nothing appear and press it again.

    function revealButton(field) {
        const b = findChild(field, "passwordRevealButton")
        verify(b, "found the reveal button")
        return b
    }

    function test_pressing_the_eye_reveals_and_hides_again() {
        const field = create({ text: "hunter2" })

        revealButton(field).clicked()
        verify(field.passwordVisible, "pressing it reveals")
        compare(field.textField.echoMode, TextInput.Normal)

        revealButton(field).clicked()
        verify(!field.passwordVisible, "and pressing it again hides")
        compare(field.textField.echoMode, TextInput.Password)
    }

    // Clicked with the mouse, not by raising clicked(): a programmatic press
    // never moves focus at all, so a case using it could not tell the caret
    // staying put from it never having moved.
    //
    // Even driven this way, the caret is held in place by two things: the
    // button is Qt.NoFocus, so it cannot take focus, and the handler puts
    // focus back anyway. Removing either alone fails nothing. Removing both
    // -- opening the focus policy and dropping the restore -- does fail
    // these two, which is the property they are here for.
    function test_pressing_the_eye_leaves_the_caret_in_the_password() {
        const field = create({ text: "hunter2" })
        field.textField.forceActiveFocus()
        waitForRendering(field)

        mouseClick(revealButton(field))
        waitForRendering(field)

        verify(field.textField.activeFocus,
               "the field still has the caret, so typing carries on")
    }

    function test_typing_after_a_reveal_goes_into_the_password() {
        // The same property, end to end, which is what the user would
        // notice: press the eye, keep typing, and the characters land.
        const field = create({ text: "hunter" })
        field.textField.forceActiveFocus()
        field.textField.cursorPosition = field.text.length
        waitForRendering(field)

        mouseClick(revealButton(field))
        waitForRendering(field)
        keyClick(Qt.Key_2)

        compare(field.text, "hunter2",
               "the keystroke went into the field, not the button")
    }
}
