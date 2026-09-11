/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The tab order every wizard step is built on.
 *
 * WizardStepBase collects the focusable controls each step registers, puts
 * the navigation buttons after them, and hands out the next and previous
 * element for Tab and Shift+Tab. Every screen in the application depends on
 * it, and the three functions that do it had never run in a test.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "StepFocusOrder"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property string networkInfoText: ""
        property var customizationSettings: ({})
    }

    Component {
        id: stepComponent

        WizardStepBase {
            id: base
            wizardContainer: fakeContainer
            width: 900
            height: 700
            title: "A step"
            subtitle: "with some controls on it"
            showBackButton: true
            showSkipButton: true

            // Three plain controls standing in for a step's own fields.
            property alias one: itemOne
            property alias two: itemTwo
            property alias three: itemThree

            content: [
                Column {
                    anchors.fill: parent
                    // activeFocusOnTab, because the rebuild skips anything
                    // that has it off -- which is how a step keeps its
                    // labels out of the order when no screen reader is
                    // running.
                    Item { id: itemOne;   objectName: "one";   width: 10; height: 10; activeFocusOnTab: true }
                    Item { id: itemTwo;   objectName: "two";   width: 10; height: 10; activeFocusOnTab: true }
                    Item { id: itemThree; objectName: "three"; width: 10; height: 10; activeFocusOnTab: true }
                }
            ]

            Component.onCompleted: {
                registerFocusGroup("fields", function () {
                    return [itemOne, itemTwo]
                }, 0)
                registerFocusGroup("extra", function () {
                    return [itemThree]
                }, 1)
            }
        }
    }

    property var step: null

    function init() {
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        step.nextButtonEnabled = true
        step.showBackButton = true
        step.showSkipButton = true
        step.rebuildFocusOrder()
        waitForRendering(step)
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
    }

    function button(name) {
        var b = findChild(step, name)
        verify(b, "found " + name)
        return b
    }

    // The three navigation buttons have no objectName of their own, so they
    // are recognised by where the order puts them: after the registered
    // controls, in the order the base appends them.
    // Everything Tab reaches after `startItem`, stopping when it comes back
    // round -- the order is a ring, so without that this walks forever.
    function orderAfter(startItem) {
        var seen = []
        var cur = startItem
        for (var i = 0; i < 20; i++) {
            cur = step.getNextFocusableElement(cur)
            if (!cur || cur === startItem || seen.indexOf(cur) !== -1)
                break
            seen.push(cur)
        }
        return seen
    }

    // -- The registered controls come first, in the order given -----------

    function test_tab_walks_the_registered_controls_in_order() {
        compare(step.getNextFocusableElement(step.one), step.two)
        compare(step.getNextFocusableElement(step.two), step.three)
    }

    function test_a_later_group_comes_after_an_earlier_one() {
        // The order argument is what a step uses to put its heading before
        // its fields, and its fields before its buttons.
        var walked = orderAfter(step.one)
        verify(walked.indexOf(step.two) < walked.indexOf(step.three),
               "the group registered second came second")
    }

    // -- And the navigation buttons are on the end ------------------------

    function test_the_navigation_buttons_are_reachable_by_tab() {
        // Without this a keyboard user reaches every field on the screen
        // and not the button that leaves it, and the wizard cannot be
        // finished without a mouse.
        var walked = orderAfter(step.one)

        verify(walked.length > 2,
               "the order carries on past the registered controls")
        var last = walked[walked.length - 1]
        verify(walked.indexOf(step.three) < walked.length - 1,
               "there is something after the last registered control")
        verify(last !== step.one && last !== step.two && last !== step.three,
               "and it is not one of the registered controls")
        compare(walked.length, 5,
                "three controls and the three navigation buttons, less the "
                + "one the walk started from")
    }

    function test_tab_comes_back_round_to_the_first_control() {
        // Off the end of the list is the beginning of it, not nothing.
        var walked = orderAfter(step.one)
        var last = walked[walked.length - 1]

        compare(step.getNextFocusableElement(last), step.one)
    }

    function test_shift_tab_goes_the_other_way() {
        compare(step.getPreviousFocusableElement(step.three), step.two)
        compare(step.getPreviousFocusableElement(step.two), step.one)
    }

    function test_shift_tab_from_the_first_control_wraps_to_the_end() {
        var walked = orderAfter(step.one)
        var last = walked[walked.length - 1]

        compare(step.getPreviousFocusableElement(step.one), last)
    }

    // -- A control that is not in the order -------------------------------

    function test_tabbing_from_something_outside_the_order_lands_in_it() {
        // Rather than returning nothing, which loses focus entirely.
        // Forwards puts the user at the start of the ring and backwards at
        // the end of it, which is what each direction would have reached
        // coming round from the other side.
        var stranger = testCase
        var walked = orderAfter(step.one)
        var last = walked[walked.length - 1]

        compare(step.getNextFocusableElement(stranger), step.one)
        compare(step.getPreviousFocusableElement(stranger), last)
    }

    // -- Buttons that are not there are not in the order ------------------

    function test_a_hidden_button_is_left_out() {
        // Tabbing onto a button that is not on the screen is worse than not
        // reaching it: the focus outline is drawn nowhere and the next key
        // press goes somewhere the user cannot see.
        var withAll = orderAfter(step.one).length

        step.showSkipButton = false
        step.showBackButton = false
        waitForRendering(step)

        var withNeither = orderAfter(step.one).length
        compare(withNeither, withAll - 2,
                "two fewer stops in the order")
    }

    function test_a_disabled_next_button_is_left_out() {
        var withIt = orderAfter(step.one).length

        step.nextButtonEnabled = false
        waitForRendering(step)

        compare(orderAfter(step.one).length, withIt - 1)
    }

    // -- Turning a group off ----------------------------------------------

    function test_a_group_turned_off_leaves_the_order() {
        // Used where a section stops applying -- a button that is not
        // available during a write, say. Its controls have to leave the tab
        // order with it, or Tab stops on something the user cannot act on.
        verify(orderAfter(step.one).indexOf(step.three) !== -1,
               "the extra group is in the order to begin with")

        step.setFocusGroupEnabled("extra", false)
        waitForRendering(step)

        compare(orderAfter(step.one).indexOf(step.three), -1,
                "and gone once the group is turned off")
    }

    function test_a_group_turned_back_on_returns_to_the_order() {
        step.setFocusGroupEnabled("extra", false)
        waitForRendering(step)
        compare(orderAfter(step.one).indexOf(step.three), -1)

        step.setFocusGroupEnabled("extra", true)
        waitForRendering(step)

        verify(orderAfter(step.one).indexOf(step.three) !== -1)
    }

    function test_turning_a_group_that_is_not_there_off_changes_nothing() {
        // A step naming a group it never registered is a typo, not a reason
        // to empty the tab order.
        var before = orderAfter(step.one).length

        step.setFocusGroupEnabled("no-such-group", false)
        waitForRendering(step)

        compare(orderAfter(step.one).length, before)
    }
}
