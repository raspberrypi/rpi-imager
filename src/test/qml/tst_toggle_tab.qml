/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * ImToggleTab: the pair of tabs choosing Wi-Fi security mode.
 *
 * Which tab is on is driven from outside -- `active` is bound to the step's
 * mode, and clicking sets the mode rather than the tab. Nothing about that
 * is visible to a screen reader except what goes into Accessible.name, so
 * that string is the whole of how a non-sighted user knows whether they are
 * about to configure a secure network or an open one.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImToggleTab"
    when: windowShown
    width: 400
    height: 200
    visible: true

    Component {
        id: tabComponent

        ImToggleTab {
            id: tab
            text: "Secure"
            property int clickCount: 0
            onClicked: tab.clickCount++
        }
    }

    function create(props) {
        const t = createTemporaryObject(tabComponent, testCase, props)
        verify(t, "the tab was created")
        return t
    }

    // -- Activating it -----------------------------------------------------

    function test_activating_it_data() {
        return [
            { tag: "click",  key: null },
            { tag: "space",  key: Qt.Key_Space },
            { tag: "return", key: Qt.Key_Return },
            { tag: "enter",  key: Qt.Key_Enter }
        ]
    }

    function test_activating_it(data) {
        // The step listens to clicked() to change mode. Unlike the toggle
        // controls, this one reaches it by every route -- Space through
        // Button's own press/release path, Return and Enter by emitting
        // clicked() rather than calling toggle().
        const t = create({})

        if (data.key === null) {
            mouseClick(t)
        } else {
            t.forceActiveFocus()
            verify(t.activeFocus)
            keyClick(data.key)
        }
        compare(t.clickCount, 1)
    }

    function test_a_disabled_tab_does_not_activate() {
        const t = create({ enabled: false })
        mouseClick(t)
        compare(t.clickCount, 0)
    }

    function test_activating_the_tab_does_not_set_active_itself() {
        // `active` is bound to the step's mode, not to the tab. A tab that
        // set its own state would fight the binding and let both tabs show
        // as selected at once.
        const t = create({ active: false })
        mouseClick(t)
        compare(t.active, false, "the step decides, not the tab")
    }

    // -- What a screen reader is told --------------------------------------

    function test_a_plain_tab_reads_as_its_label() {
        const t = create({ text: "Open", accessibleDescription: "" })
        compare(t.Accessible.name, "Open")
    }

    function test_the_selected_tab_says_so() {
        // Without this a screen reader user has no way to tell which of the
        // two is currently chosen -- the difference is otherwise a colour.
        const t = create({ text: "Secure", active: true })
        compare(t.Accessible.name, "Secure (currently selected)")
    }

    function test_a_description_is_read_after_the_label() {
        const t = create({ text: "Open", accessibleDescription: "No password" })
        compare(t.Accessible.name, "Open, No password")
    }

    function test_a_selected_tab_with_a_description_reads_both() {
        const t = create({
            text: "Secure", active: true, accessibleDescription: "WPA2"
        })
        compare(t.Accessible.name, "Secure, WPA2 (currently selected)")
    }

    function test_a_disabled_tab_says_so() {
        const t = create({ text: "Secure", enabled: false })
        compare(t.Accessible.name, "Secure (disabled)")
    }

    function test_a_disabled_tab_with_a_description_reads_both() {
        const t = create({
            text: "Secure", enabled: false, accessibleDescription: "WPA2"
        })
        compare(t.Accessible.name, "Secure, WPA2 (disabled)")
    }

    function test_disabled_wins_over_selected() {
        // Both at once is reachable while the step is loading. Being unable
        // to use the control is the more urgent of the two facts.
        const t = create({ text: "Secure", active: true, enabled: false })
        compare(t.Accessible.name, "Secure (disabled)")
    }

    function test_the_name_follows_the_tab_changing() {
        // `active` moves when the other tab is clicked, so the announcement
        // has to be a binding rather than a value read once.
        const t = create({ text: "Secure", active: false })
        compare(t.Accessible.name, "Secure")

        t.active = true
        compare(t.Accessible.name, "Secure (currently selected)")

        t.active = false
        compare(t.Accessible.name, "Secure")
    }
}
