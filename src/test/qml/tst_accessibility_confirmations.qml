/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Whether a confirmation dialog is raised, given who is asking.
 *
 * "Exclude system drives" is the safety filter on the storage step, and
 * unchecking it puts the machine's own disks into the list of things that
 * can be written to. That has a confirmation behind it, and confirmations
 * of this kind exist to slow down someone who has not registered what the
 * control does.
 *
 * A screen reader user has been read the control's label, its role and its
 * accessible description before activating it -- more than a sighted mouse
 * user is ever shown. The dialog would interrupt the person who was told
 * the most in order to protect the person who was told the least, so it is
 * skipped when an assistive technology is attached and the warning lives in
 * the description instead, where it is spoken.
 *
 * Skipped, not removed: everyone else still gets it, and choosing a system
 * drive still goes through the type-its-name dialog for everybody.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "AccessibilityConfirmations"
    when: windowShown
    width: 400
    height: 200
    visible: true

    // The storage step's decision, in the shape it has there: the filter
    // checkbox, a deployment-wide opt-out, and the accessibility check.
    Component {
        id: gateComponent

        ImCheckBox {
            id: gate
            text: "Exclude system drives"
            checked: true

            property bool disableWarnings: false
            property int confirmationsRaised: 0
            property int filterDisabledCount: 0

            onToggled: {
                if (!gate.checked) {
                    var skipConfirmation = gate.disableWarnings
                                        || PlatformHelper.assistiveTechnologyActive
                    if (skipConfirmation)
                        gate.filterDisabledCount++
                    else
                        gate.confirmationsRaised++
                }
            }
        }
    }

    function create(props) {
        const gate = createTemporaryObject(gateComponent, testCase, props)
        verify(gate, "the filter checkbox was created")
        return gate
    }

    function uncheckWith(gate, key) {
        gate.forceActiveFocus()
        keyClick(key)
    }

    function cleanup() {
        // Never leave the process claiming an assistive technology is
        // attached: the flag is global and the next test case would inherit
        // it, quietly passing for the wrong reason.
        TestAccessibility.setActive(false)
    }

    // -- The property itself -----------------------------------------------

    function test_no_assistive_technology_is_reported_by_default() {
        compare(PlatformHelper.assistiveTechnologyActive, false)
    }

    function test_the_property_reports_an_attached_assistive_technology() {
        TestAccessibility.setActive(true)
        compare(PlatformHelper.assistiveTechnologyActive, true)

        TestAccessibility.setActive(false)
        compare(PlatformHelper.assistiveTechnologyActive, false)
    }

    function test_the_property_notifies_when_it_changes() {
        // A screen reader can be started partway through a session, so this
        // cannot be a constant: anything bound to it has to follow.
        const spy = spyComponent.createObject(testCase, {
            target: PlatformHelper,
            signalName: "assistiveTechnologyActiveChanged"
        })
        verify(spy)

        TestAccessibility.setActive(true)
        compare(spy.count, 1)

        TestAccessibility.setActive(false)
        compare(spy.count, 2)
        spy.destroy()
    }

    // -- Everyone else still gets the dialog -------------------------------

    function test_unchecking_the_filter_confirms_data() {
        return [
            { tag: "space",  key: Qt.Key_Space },
            { tag: "return", key: Qt.Key_Return },
            { tag: "enter",  key: Qt.Key_Enter }
        ]
    }

    function test_unchecking_the_filter_confirms(data) {
        const gate = create({})
        uncheckWith(gate, data.key)

        compare(gate.checked, false)
        compare(gate.confirmationsRaised, 1, "the dialog was raised")
        compare(gate.filterDisabledCount, 0, "and the filter did not just go")
    }

    function test_unchecking_the_filter_by_mouse_confirms() {
        const gate = create({})
        mouseClick(gate)

        compare(gate.checked, false)
        compare(gate.confirmationsRaised, 1)
    }

    // -- An assistive technology skips it ----------------------------------

    function test_an_attached_assistive_technology_skips_the_dialog_data() {
        return test_unchecking_the_filter_confirms_data()
    }

    function test_an_attached_assistive_technology_skips_the_dialog(data) {
        TestAccessibility.setActive(true)

        const gate = create({})
        uncheckWith(gate, data.key)

        compare(gate.checked, false, "the filter came off")
        compare(gate.confirmationsRaised, 0, "with no dialog in the way")
        compare(gate.filterDisabledCount, 1, "and focus moved to the list")
    }

    function test_the_skip_applies_to_a_mouse_too() {
        // Attached is attached. A switch access device or a magnifier user
        // may well be clicking, and the reason to skip is that the control
        // has already been announced, not which input device was used.
        TestAccessibility.setActive(true)

        const gate = create({})
        mouseClick(gate)
        compare(gate.confirmationsRaised, 0)
    }

    function test_starting_a_screen_reader_mid_session_takes_effect() {
        const gate = create({})

        TestAccessibility.setActive(true)
        uncheckWith(gate, Qt.Key_Space)
        compare(gate.confirmationsRaised, 0, "the new state is what counts")
    }

    function test_stopping_a_screen_reader_mid_session_restores_the_dialog() {
        TestAccessibility.setActive(true)
        const gate = create({})

        TestAccessibility.setActive(false)
        uncheckWith(gate, Qt.Key_Space)
        compare(gate.confirmationsRaised, 1)
    }

    // -- Re-checking the filter is the safe direction ----------------------

    function test_turning_the_filter_back_on_never_confirms() {
        const gate = create({ checked: false })
        uncheckWith(gate, Qt.Key_Space)

        compare(gate.checked, true)
        compare(gate.confirmationsRaised, 0,
                "putting the safety back does not need confirming")
    }

    // -- The deployment-wide opt-out is unaffected -------------------------

    function test_disable_warnings_still_skips_on_its_own() {
        const gate = create({ disableWarnings: true })
        uncheckWith(gate, Qt.Key_Space)

        compare(gate.confirmationsRaised, 0)
        compare(gate.filterDisabledCount, 1)
    }

    Component {
        id: spyComponent
        SignalSpy {}
    }
}
