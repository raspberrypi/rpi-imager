/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * ImOptionButton: a labelled row with an action button and an optional
 * "learn more" link.
 *
 * The label, the button's own text and the explanation are three separate
 * strings sitting next to each other on screen. A sighted user reads them
 * as one row; a screen reader reads whatever Accessible.name composes, so
 * that composition is the whole of the row for anyone not looking at it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImOptionButton"
    when: windowShown
    width: 500
    height: 200
    visible: true

    Component {
        id: rowComponent

        ImOptionButton {
            id: row
            width: 400
            text: "SSH keys"
            btnText: "MANAGE"

            property int clickCount: 0
            onClicked: row.clickCount++
        }
    }

    function create(props) {
        const r = createTemporaryObject(rowComponent, testCase, props)
        verify(r, "the row was created")
        return r
    }

    // -- The button reaches the step ---------------------------------------

    function test_pressing_the_button_data() {
        return [
            { tag: "click",  key: null },
            { tag: "space",  key: Qt.Key_Space },
            { tag: "return", key: Qt.Key_Return },
            { tag: "enter",  key: Qt.Key_Enter }
        ]
    }

    function test_pressing_the_button(data) {
        // The row's own clicked() is what the step connects to; the inner
        // button forwards to it. Every route has to arrive.
        const r = create({})

        if (data.key === null) {
            mouseClick(r.focusItem)
        } else {
            r.forceActiveFocus()
            verify(r.focusItem.activeFocus)
            keyClick(data.key)
        }
        compare(r.clickCount, 1)
    }

    function test_a_disabled_row_does_not_fire() {
        const r = create({ enabled: false })
        mouseClick(r.focusItem)
        compare(r.clickCount, 0)
    }

    function test_enabled_reaches_the_inner_button() {
        // `enabled` is an alias onto the button rather than the wrapper, so
        // a caller disabling the row has to actually disable the control.
        const r = create({ enabled: false })
        compare(r.focusItem.enabled, false)

        r.enabled = true
        compare(r.focusItem.enabled, true)
    }

    // -- Focus -------------------------------------------------------------

    function test_focus_lands_on_the_button_not_the_row() {
        const r = create({})
        r.forceActiveFocus()
        verify(r.focusItem.activeFocus)
    }

    // -- What a screen reader is given -------------------------------------

    function test_the_label_and_the_button_text_are_read_together() {
        // "SSH keys" and "MANAGE" are separate items on screen. Read alone,
        // neither says what pressing it does.
        const r = create({})
        compare(r.focusItem.Accessible.name, "SSH keys - MANAGE")
    }

    function test_a_description_is_appended() {
        const r = create({ accessibleDescription: "Add or remove authorised keys" })
        compare(r.focusItem.Accessible.name,
                "SSH keys - MANAGE, Add or remove authorised keys")
    }

    function test_the_help_label_is_used_when_there_is_no_description() {
        const r = create({ helpLabel: "Learn more about SSH keys" })
        compare(r.focusItem.Accessible.name,
                "SSH keys - MANAGE, Learn more about SSH keys")
    }

    function test_a_description_wins_over_the_help_label() {
        const r = create({
            accessibleDescription: "Add or remove authorised keys",
            helpLabel: "Learn more about SSH keys"
        })
        compare(r.focusItem.Accessible.name,
                "SSH keys - MANAGE, Add or remove authorised keys")
    }

    function test_the_name_follows_the_button_text_changing() {
        // The action label changes with state -- MANAGE against ADD, say.
        const r = create({})
        r.btnText = "ADD"
        compare(r.focusItem.Accessible.name, "SSH keys - ADD")
    }

    // -- The help link -----------------------------------------------------

    function test_the_help_link_needs_both_a_label_and_a_url() {
        // Half of a link is a piece of text that looks clickable and is not.
        const r = create({})
        verify(!r.helpLinkItem.visible, "neither given")

        r.helpLabel = "Learn more"
        verify(!r.helpLinkItem.visible, "a label with nowhere to go")

        r.helpLabel = ""
        r.helpUrl = "https://example.invalid/help"
        verify(!r.helpLinkItem.visible, "a url with nothing to click")

        r.helpLabel = "Learn more"
        verify(r.helpLinkItem.visible, "both, so it is offered")
    }

    function test_the_help_link_shows_the_label_it_was_given() {
        const r = create({
            helpLabel: "Learn more about SSH keys",
            helpUrl: "https://example.invalid/help"
        })
        compare(r.helpLinkItem.text, "Learn more about SSH keys")
    }

    // -- Activating the help link ------------------------------------------
    //
    // Five routes reach the same action: the pointer, Enter, Return, Space and
    // the accessibility press action. Until openHelpLink() collected them they
    // were five copies of the same four lines, which is how one of them gets
    // left off a newly added row -- and a keyboard user then has a link they
    // can focus and cannot open.

    Component {
        id: rowWithHelpComponent

        ImOptionButton {
            id: helpRow
            width: 400
            text: "SSH keys"
            btnText: "MANAGE"
            helpLabel: "Learn more about SSH keys"
            helpUrl: "https://example.invalid/ssh-help"

            property int opened: 0
            property int clickCount: 0
            onClicked: helpRow.clickCount++
            function openHelpLink() { helpRow.opened++ }
        }
    }

    function createWithHelp() {
        const r = createTemporaryObject(rowWithHelpComponent, testCase)
        verify(r, "the row was created")
        verify(r.helpLinkItem.visible, "the link is offered")
        waitForRendering(r)
        return r
    }

    function test_every_route_to_the_help_link_data() {
        return [
            { tag: "pointer",      how: "click" },
            { tag: "space",        how: "key", key: Qt.Key_Space },
            { tag: "return",       how: "key", key: Qt.Key_Return },
            { tag: "enter",        how: "key", key: Qt.Key_Enter },
            { tag: "press action", how: "accessible" }
        ]
    }

    function test_every_route_to_the_help_link(data) {
        const r = createWithHelp()
        const link = r.helpLinkItem

        if (data.how === "click") {
            mouseClick(link)
        } else if (data.how === "key") {
            link.forceActiveFocus()
            verify(link.activeFocus, "the link takes focus")
            keyClick(data.key)
        } else {
            // What a screen reader does with the link. Accessible's actions
            // are signals, so a test raises one the same way.
            link.Accessible.pressAction()
        }

        compare(r.opened, 1,
                data.tag + " has to open the help link, or a user who reaches "
                + "it that way has a link that does nothing")
    }

    function test_reading_the_documentation_is_not_pressing_the_button() {
        // The link sits under the label, the action button to its right. The
        // row's clicked() is what the step acts on -- selecting a key file,
        // clearing a registration -- and opening a help page must not be
        // mistaken for asking for it.
        const r = createWithHelp()

        mouseClick(r.helpLinkItem)

        compare(r.opened, 1, "the link opened")
        compare(r.clickCount, 0, "and the row's own action did not fire")
    }
}
