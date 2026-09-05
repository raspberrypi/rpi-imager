/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * ImCheckBox, and the one that guards the storage list.
 *
 * Four of these ship. Three are ordinary settings. The fourth is
 * "Exclude system drives" on the storage step, and unchecking it is what
 * puts the machine's own disks into the list of things that can be written
 * to. That step raises its confirmation dialog from onToggled.
 *
 * Every keyboard route -- Space, Return and Enter -- called
 * AbstractButton::toggle(), which changes `checked` and emits nothing.
 * So the box unchecked, the system drives appeared, and the confirmation
 * never opened. Space was broken for the same reason as the other two: the
 * explicit handler shadowed the native handling that would have gone
 * through the click path.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImCheckBox"
    when: windowShown
    width: 400
    height: 200
    visible: true

    Component {
        id: boxComponent

        ImCheckBox {
            id: box
            text: "Exclude system drives"

            property int toggledCount: 0
            property int checkedChangedCount: 0

            onToggled: box.toggledCount++
            onCheckedChanged: box.checkedChangedCount++
        }
    }

    // Stands in for the storage step: the confirmation before system drives
    // are listed hangs off onToggled, not onCheckedChanged.
    Component {
        id: guardComponent

        ImCheckBox {
            id: guard
            text: "Exclude system drives"
            checked: true

            property int confirmationsRaised: 0

            onToggled: {
                if (!guard.checked)
                    guard.confirmationsRaised++
            }
        }
    }

    function create(props) {
        const box = createTemporaryObject(boxComponent, testCase, props)
        verify(box, "the checkbox was created")
        return box
    }

    function activate(control, key) {
        if (key === null) {
            mouseClick(control)
        } else {
            control.forceActiveFocus()
            verify(control.activeFocus)
            keyClick(key)
        }
    }

    function inputs() {
        return [
            { tag: "click",  key: null },
            { tag: "space",  key: Qt.Key_Space },
            { tag: "return", key: Qt.Key_Return },
            { tag: "enter",  key: Qt.Key_Enter }
        ]
    }

    // -- Every route behaves the same --------------------------------------

    function test_checking_it_data() { return inputs() }

    function test_checking_it(data) {
        const box = create({ checked: false })
        activate(box, data.key)

        compare(box.checked, true)
        compare(box.toggledCount, 1, "the listener was told exactly once")
        compare(box.checkedChangedCount, 1)
    }

    function test_unchecking_it_data() { return inputs() }

    function test_unchecking_it(data) {
        const box = create({ checked: true })
        activate(box, data.key)

        compare(box.checked, false)
        compare(box.toggledCount, 1)
    }

    function test_flipping_it_twice_returns_to_where_it_started_data() {
        return inputs()
    }

    function test_flipping_it_twice_returns_to_where_it_started(data) {
        const box = create({ checked: false })
        activate(box, data.key)
        activate(box, data.key)

        compare(box.checked, false)
        compare(box.toggledCount, 2, "twice, not once and not three times")
    }

    // -- The storage safety gate -------------------------------------------

    function test_the_system_drive_guard_confirms_however_it_is_unchecked_data() {
        return inputs()
    }

    function test_the_system_drive_guard_confirms_however_it_is_unchecked(data) {
        // The one that matters. Whichever way the user unchecks it, the step
        // has to get the chance to ask them whether they meant it.
        const guard = createTemporaryObject(guardComponent, testCase, {})
        verify(guard)
        compare(guard.checked, true)

        activate(guard, data.key)

        compare(guard.checked, false, "the filter is off")
        compare(guard.confirmationsRaised, 1,
                "and the step was given the chance to confirm")
    }

    function test_rechecking_the_guard_raises_no_confirmation() {
        // Turning the filter back on is the safe direction.
        const guard = createTemporaryObject(guardComponent, testCase, {})
        activate(guard, Qt.Key_Space)
        compare(guard.confirmationsRaised, 1)

        activate(guard, Qt.Key_Space)
        compare(guard.checked, true)
        compare(guard.confirmationsRaised, 1, "no second confirmation")
    }

    // -- Programmatic changes are not user changes -------------------------

    function test_setting_the_property_does_not_report_a_user_toggle() {
        const box = create({ checked: false })
        box.checked = true

        compare(box.toggledCount, 0,
                "the step must not react to its own write")
        compare(box.checkedChangedCount, 1,
                "though the value did change, and listeners on that hear it")
    }

    function test_a_disabled_box_does_not_flip_when_clicked() {
        const box = create({ checked: false, enabled: false })
        mouseClick(box)

        compare(box.checked, false)
        compare(box.toggledCount, 0)
    }

    // -- Accessibility -----------------------------------------------------

    function test_the_accessible_state_tracks_the_box() {
        const box = create({ checked: false })
        compare(box.Accessible.checked, false)

        activate(box, Qt.Key_Space)
        compare(box.Accessible.checked, true)
    }

    function test_the_label_is_the_accessible_name() {
        const box = create({ text: "Enable telemetry" })
        compare(box.Accessible.name, "Enable telemetry")
    }
}
