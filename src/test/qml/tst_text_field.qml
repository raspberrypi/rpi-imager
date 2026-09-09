/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * ImTextField: the hostname, username, SSID and token fields.
 *
 * Everything here has already gone wrong once in a shipped release. Qt's
 * paste inserts the clipboard verbatim, so text copied out of a browser
 * arrives with a trailing newline: that corrupted a password hash (#1627)
 * and a repository URL (#1687). And `value` could not be a binding on
 * `text`, because a use-site onTextChanged would then read the previous
 * contents -- which silently threw away the Connect token the browser had
 * just filled in, leaving Next disabled with nothing on screen to say why.
 *
 * None of that is reachable from C++. It only runs when a control is
 * instantiated and something puts text into it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImTextField"
    when: windowShown
    width: 400
    height: 120
    visible: true

    readonly property string newline: String.fromCharCode(10)
    readonly property string tab: String.fromCharCode(9)
    readonly property string nul: String.fromCharCode(0)
    readonly property string del: String.fromCharCode(127)

    Component {
        id: fieldComponent
        ImTextField {
            width: 300
        }
    }

    // A field with a use-site handler on the same signal the scrubber uses.
    // Handlers run in declaration order, base before derived, so this stands
    // in for what a wizard step's own onTextChanged sees.
    Component {
        id: watchedComponent
        ImTextField {
            width: 300
            property string seenValue: "not called"
            property string seenText: "not called"
            onTextChanged: {
                seenValue = value
                seenText = text
            }
        }
    }

    function create(props) {
        const field = createTemporaryObject(fieldComponent, testCase, props)
        verify(field, "the text field was created")
        return field
    }

    // -- Typing ------------------------------------------------------------

    function test_typed_text_reaches_both_text_and_value() {
        const field = create({})
        field.forceActiveFocus()
        verify(field.activeFocus)

        keyClick(Qt.Key_P)
        keyClick(Qt.Key_I)
        compare(field.text, "pi")
        compare(field.value, "pi")
    }

    function test_a_space_typed_mid_phrase_is_not_swallowed() {
        // Trimming deliberately happens when `value` is read rather than in
        // the scrubber, so that a field which trims does not fight the person
        // typing "My Network" one character at a time.
        const field = create({ trimWhitespace: true })
        field.forceActiveFocus()

        keyClick(Qt.Key_M)
        keyClick(Qt.Key_Y)
        keyClick(Qt.Key_Space)
        compare(field.text, "my ", "the space stays in the field while typing")

        keyClick(Qt.Key_W)
        keyClick(Qt.Key_I)
        keyClick(Qt.Key_F)
        keyClick(Qt.Key_I)
        compare(field.text, "my wifi")
        compare(field.value, "my wifi")
    }

    // -- Scrubbing ---------------------------------------------------------

    function test_a_pasted_trailing_newline_is_removed() {
        const field = create({})
        field.text = "https://example.invalid/repo" + newline
        compare(field.text, "https://example.invalid/repo")
        compare(field.value, "https://example.invalid/repo")
    }

    function test_every_control_character_is_removed() {
        const field = create({})
        field.text = "a" + nul + "b" + tab + "c" + newline + "d" + del + "e"
        compare(field.text, "abcde")
        compare(field.value, "abcde")
    }

    function test_a_string_that_is_only_control_characters_empties_the_field() {
        const field = create({})
        field.text = newline + tab + del
        compare(field.text, "")
        compare(field.value, "")
    }

    function test_text_with_nothing_to_scrub_is_left_exactly_alone() {
        const field = create({})
        const clean = "pi@raspberrypi.local:8080/path?q=1&r=2"
        field.text = clean
        compare(field.text, clean)
        compare(field.value, clean)
    }

    function test_scrubbing_never_leaves_the_caret_past_the_end() {
        // The scrub reassigns `text`, and the caret is clamped to the length
        // of what is left. Assigning wholesale puts the caret at the end, as
        // a paste does, so the guarantee that matters is that it lands on the
        // last character rather than pointing beyond the string.
        const field = create({})
        field.forceActiveFocus()

        field.text = "abc" + newline + "def" + newline
        compare(field.text, "abcdef")
        compare(field.cursorPosition, field.text.length,
                "the caret sits at the end of the cleaned text")
        verify(field.cursorPosition <= field.text.length)
    }

    function test_typing_after_a_scrub_appends_rather_than_overwrites() {
        // What the caret clamp is for: the next keystroke has to land
        // somewhere valid.
        const field = create({})
        field.forceActiveFocus()
        field.text = "abc" + newline

        keyClick(Qt.Key_D)
        compare(field.text, "abcd")
    }

    // -- value keeps step with text ----------------------------------------

    function test_a_use_site_handler_sees_the_new_value_not_the_old_one() {
        // The #1687 shape. A wizard step's own onTextChanged runs after the
        // scrubber's; if `value` were a binding it would still hold whatever
        // was in the field before this change, and the step would store that.
        const watched = createTemporaryObject(watchedComponent, testCase, {})
        verify(watched)

        watched.text = "token-one"
        compare(watched.seenText, "token-one")
        compare(watched.seenValue, "token-one",
                "value is current by the time the use site runs")

        watched.text = "token-two"
        compare(watched.seenValue, "token-two",
                "and again on a second change, not one behind")
    }

    function test_a_use_site_handler_sees_the_scrubbed_value() {
        // The handler must never be shown the version with the newline in it
        // as its final word: the scrub re-enters, and the last thing the use
        // site sees is the cleaned text.
        const watched = createTemporaryObject(watchedComponent, testCase, {})
        verify(watched)

        watched.text = "token" + newline
        compare(watched.seenText, "token")
        compare(watched.seenValue, "token")
    }

    // -- Trimming ----------------------------------------------------------

    function test_a_trimming_field_trims_value_but_not_text() {
        // `text` is what is on screen and the caret moves through it; `value`
        // is what the caller stores. Only the latter is trimmed.
        const field = create({ trimWhitespace: true, text: "  raspberrypi  " })
        compare(field.text, "  raspberrypi  ")
        compare(field.value, "raspberrypi")
    }

    function test_a_non_trimming_field_keeps_surrounding_space() {
        const field = create({ text: "  spaced  " })
        compare(field.value, "  spaced  ")
    }

    function test_turning_trimming_on_resyncs_what_is_already_there() {
        const field = create({ text: "  hostname  " })
        compare(field.value, "  hostname  ")

        field.trimWhitespace = true
        compare(field.value, "hostname",
                "the existing contents are re-read, not left stale")

        field.trimWhitespace = false
        compare(field.value, "  hostname  ")
    }

    function test_value_is_populated_before_anything_reads_it() {
        // Set at construction rather than typed: a field created with text
        // already in it must not report an empty value until first edit.
        const field = create({ text: "preset", trimWhitespace: true })
        compare(field.value, "preset")
    }

    function test_a_field_of_only_spaces_trims_to_nothing() {
        const field = create({ trimWhitespace: true, text: "     " })
        compare(field.text, "     ")
        compare(field.value, "", "a hostname of spaces is no hostname")
    }


    // -- The right-click menu ----------------------------------------------
    //
    // Cut and Copy were uncovered. What they do is ordinary; when they are
    // offered is not, and that is the part worth pinning.
    //
    // This field is used for the Wi-Fi passphrase and the user password as
    // well as the hostname, and it can be set to show dots instead of
    // characters. Both items are refused on a masked field: a passphrase the
    // user cannot see is one they cannot lift out through a context menu
    // either, and the same menu is a click away on somebody else's machine
    // that has been left at the customisation screen.
    //
    // Cut is refused on a read-only field too -- the repository path is one
    // -- because removing text from a field the user cannot type into leaves
    // them with no way to put it back.

    function menuItem(field, name) {
        const item = findChild(field, name)
        verify(item, "found " + name)
        return item
    }

    function test_copy_puts_the_selection_on_the_clipboard() {
        const field = create({ text: "pi-in-the-shed" })
        field.select(0, 2)
        compare(field.selectedText, "pi")

        menuItem(field, "textFieldCopyItem").triggered()

        compare(ClipboardHelper.getText(), "pi")
        compare(field.text, "pi-in-the-shed", "and leaves the field alone")
    }

    function test_cut_puts_it_on_the_clipboard_and_takes_it_out() {
        const field = create({ text: "pi-in-the-shed" })
        field.select(0, 3)
        compare(field.selectedText, "pi-")

        menuItem(field, "textFieldCutItem").triggered()

        compare(ClipboardHelper.getText(), "pi-",
                "it went to the clipboard")
        compare(field.text, "in-the-shed",
                "and came out of the field")
    }

    function test_neither_is_offered_with_nothing_selected() {
        const field = create({ text: "pi-in-the-shed" })
        compare(field.selectedText, "")

        verify(!menuItem(field, "textFieldCutItem").enabled)
        verify(!menuItem(field, "textFieldCopyItem").enabled)
    }

    function test_neither_is_offered_on_a_field_showing_dots() {
        // The privacy one. A field masked because it holds a passphrase
        // must not hand that passphrase to the clipboard.
        const field = create({ text: "correct horse battery" })
        field.echoMode = TextInput.Password
        field.selectAll()
        verify(field.selectedText.length > 0, "there is a selection")

        verify(!menuItem(field, "textFieldCopyItem").enabled,
               "a masked field cannot be copied out of")
        verify(!menuItem(field, "textFieldCutItem").enabled,
               "nor cut out of")
    }

    function test_cut_is_not_offered_on_a_field_that_cannot_be_typed_into() {
        const field = create({ text: "/home/pi/os_list.json", readOnly: true })
        field.selectAll()
        verify(field.selectedText.length > 0)

        verify(!menuItem(field, "textFieldCutItem").enabled,
               "nothing can be removed from a read-only field")
        verify(menuItem(field, "textFieldCopyItem").enabled,
               "but it can still be copied, which is the point of showing it")
    }

    function test_select_all_and_paste_are_offered_when_they_apply() {
        // The two either side of them, so the menu is not simply inert.
        const field = create({ text: "pi-in-the-shed" })

        verify(menuItem(field, "textFieldSelectAllItem").enabled,
               "there is something to select")

        const empty = create({})
        verify(!menuItem(empty, "textFieldSelectAllItem").enabled,
               "and nothing to select in an empty field")
    }
}
