/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * ImRadioButton.
 *
 * This one is the counter-example to ImCheckBox and ImOptionPill: its key
 * handlers call click(), which goes through the same path a mouse does and
 * emits both clicked() and toggled(). The comment beside it even says so.
 * Pinning that here so it stays that way, since the two controls beside it
 * in the same directory had drifted to toggle() and stopped telling anyone.
 *
 * The other half is the guard against unchecking. A radio in a group is
 * turned off by another one being turned on, never by being activated
 * again -- pressing Space twice on the same option must not leave the group
 * with nothing selected.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImRadioButton"
    when: windowShown
    width: 400
    height: 200
    visible: true

    Component {
        id: buttonComponent

        ImRadioButton {
            id: btn
            text: "Use password authentication"
            property int clickCount: 0
            property int toggledCount: 0
            onClicked: btn.clickCount++
            onToggled: btn.toggledCount++
        }
    }

    // Two in a group, the way they are actually used.
    Component {
        id: groupComponent

        // A ColumnLayout, not a bare Item: without one the two radios are
        // both at (0,0) and a mouseClick aimed at the first lands on the
        // second, which is drawn over it.
        ColumnLayout {
            property alias first: one
            property alias second: two
            property ButtonGroup group: ButtonGroup { id: grp }

            ImRadioButton {
                id: one
                text: "Password"
                checked: true
                ButtonGroup.group: grp
            }
            ImRadioButton {
                id: two
                text: "SSH key"
                ButtonGroup.group: grp
            }
        }
    }

    function create(props) {
        const b = createTemporaryObject(buttonComponent, testCase, props)
        verify(b, "the radio button was created")
        return b
    }

    function inputs() {
        return [
            { tag: "click",  key: null },
            { tag: "space",  key: Qt.Key_Space },
            { tag: "return", key: Qt.Key_Return },
            { tag: "enter",  key: Qt.Key_Enter }
        ]
    }

    function activate(b, key) {
        if (key === null) {
            mouseClick(b)
        } else {
            b.forceActiveFocus()
            verify(b.activeFocus)
            keyClick(key)
        }
    }

    // -- Choosing an option ------------------------------------------------

    function test_choosing_it_data() { return inputs() }

    function test_choosing_it(data) {
        const b = create({ checked: false })
        activate(b, data.key)

        compare(b.checked, true)
        compare(b.clickCount, 1, "the click path was used, not toggle()")
        compare(b.toggledCount, 1, "so anything listening for a change heard it")
    }

    // -- It cannot be turned off by choosing it again ----------------------

    function test_choosing_it_again_leaves_it_on_data() { return inputs() }

    function test_choosing_it_again_leaves_it_on(data) {
        // A radio group with nothing selected is a state the user cannot get
        // back out of by any means the group offers.
        const b = create({ checked: true })
        activate(b, data.key)

        compare(b.checked, true, "still chosen")
        compare(b.toggledCount, 0, "and nothing changed, so nothing was announced")
    }

    function test_a_redundant_activation_reports_differently_by_route() {
        // Worth knowing rather than relying on. The key handlers guard with
        // `if (!checked)`, so a redundant Space emits nothing at all; a
        // redundant mouse click goes through AbstractButton and emits
        // clicked() as Qt does everywhere. Neither changes the selection.
        //
        // Nothing consumes clicked() on these today -- RemoteAccessStep and
        // RepositoryDialog both read `checked` or listen to
        // onCheckedChanged -- so the difference is invisible. It would stop
        // being invisible the moment someone added an onClicked with a side
        // effect, which is why it is written down here.
        const byKey = create({ checked: true })
        activate(byKey, Qt.Key_Space)
        compare(byKey.clickCount, 0, "the keyboard guard suppresses it")
        compare(byKey.checked, true)

        const byMouse = create({ checked: true })
        mouseClick(byMouse)
        compare(byMouse.clickCount, 1, "the mouse does not")
        compare(byMouse.checked, true, "but the selection is unchanged either way")
    }

    // -- In a group --------------------------------------------------------

    function test_choosing_the_other_option_moves_the_selection() {
        const g = createTemporaryObject(groupComponent, testCase, {})
        verify(g)
        compare(g.first.checked, true)

        g.second.forceActiveFocus()
        keyClick(Qt.Key_Space)

        compare(g.second.checked, true)
        compare(g.first.checked, false, "the group has exactly one selection")
    }

    function test_the_group_always_has_something_selected() {
        const g = createTemporaryObject(groupComponent, testCase, {})
        verify(g)

        // Hammer the already-selected one from every angle.
        g.first.forceActiveFocus()
        keyClick(Qt.Key_Space)
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Enter)
        mouseClick(g.first)

        verify(g.first.checked || g.second.checked,
               "something is still chosen")
        compare(g.first.checked, true)
    }

    // -- Disabled ----------------------------------------------------------

    function test_a_disabled_option_cannot_be_chosen() {
        const b = create({ checked: false, enabled: false })
        mouseClick(b)

        compare(b.checked, false)
        compare(b.clickCount, 0)
    }

    // -- Accessibility -----------------------------------------------------

    function test_the_accessible_state_tracks_the_selection() {
        const b = create({ checked: false })
        compare(b.Accessible.checked, false)

        activate(b, Qt.Key_Space)
        compare(b.Accessible.checked, true)
    }
}
