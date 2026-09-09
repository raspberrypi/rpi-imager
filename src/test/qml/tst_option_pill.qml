/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * ImOptionPill: the on/off switches on the customisation step.
 *
 * Enable SSH, set a username, configure wireless. What the switch is drawn
 * as and what gets written to the card have to be the same thing -- a pill
 * showing "off" while the option is on is a setting the user never agreed
 * to, discovered only once the machine is booted and headless.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ImOptionPill"
    when: windowShown
    width: 400
    height: 200
    visible: true

    Component {
        id: pillComponent

        ImOptionPill {
            id: pill
            width: 350
            text: "Enable SSH"

            property int toggledCount: 0
            property bool lastToggledValue: false

            onToggled: function(value) {
                pill.toggledCount++
                pill.lastToggledValue = value
            }
        }
    }

    function create(props) {
        const pill = createTemporaryObject(pillComponent, testCase, props)
        verify(pill, "the pill was created")
        return pill
    }

    // -- The switch and the property agree ---------------------------------

    function test_a_pill_starts_off_by_default() {
        const pill = create({})
        compare(pill.checked, false)
        compare(pill.focusItem.checked, false)
    }

    function test_a_pill_created_on_is_drawn_on() {
        const pill = create({ checked: true })
        compare(pill.checked, true)
        compare(pill.focusItem.checked, true,
                "the switch shows what the property says")
    }

    function test_setting_the_property_moves_the_switch() {
        const pill = create({})
        pill.checked = true
        compare(pill.focusItem.checked, true)

        pill.checked = false
        compare(pill.focusItem.checked, false)
    }

    // -- Every way a user can flip it does the same thing -------------------
    //
    // Return and Enter used to call AbstractButton::toggle(), which moves the
    // switch and emits nothing. The switch showed "on" while pill.checked
    // stayed false and the step was never told -- so the option the user
    // turned on was not the option written to the card.

    function test_flipping_it_by_mouse_or_key_data() {
        return [
            { tag: "click",  key: null },
            { tag: "space",  key: Qt.Key_Space },
            { tag: "return", key: Qt.Key_Return },
            { tag: "enter",  key: Qt.Key_Enter }
        ]
    }

    function test_flipping_it_by_mouse_or_key(data) {
        const pill = create({})

        if (data.key === null) {
            mouseClick(pill.focusItem)
        } else {
            pill.forceActiveFocus()
            verify(pill.focusItem.activeFocus)
            keyClick(data.key)
        }

        compare(pill.checked, true, "the property agrees with the switch")
        compare(pill.focusItem.checked, true, "and the switch is drawn on")
        compare(pill.toggledCount, 1, "and the step was told once")
        compare(pill.lastToggledValue, true)
    }

    function test_flipping_it_back_by_mouse_or_key_data() {
        return test_flipping_it_by_mouse_or_key_data()
    }

    function test_flipping_it_back_by_mouse_or_key(data) {
        const pill = create({ checked: true })

        if (data.key === null) {
            mouseClick(pill.focusItem)
        } else {
            pill.forceActiveFocus()
            keyClick(data.key)
        }

        compare(pill.checked, false)
        compare(pill.focusItem.checked, false)
        compare(pill.toggledCount, 1)
        compare(pill.lastToggledValue, false)
    }

    // -- The property still drives the switch after a manual flip ----------

    function test_the_switch_follows_the_property_after_being_clicked() {
        // The step sets these programmatically -- choosing password
        // authentication turns SSH on, loading a saved profile resets every
        // option at once -- so the binding has to survive being touched.
        const pill = create({})

        mouseClick(pill.focusItem)
        compare(pill.checked, true)
        compare(pill.focusItem.checked, true)

        pill.checked = false
        compare(pill.focusItem.checked, false,
                "the switch went back to off with the property")
    }

    function test_the_switch_follows_a_reset_after_several_flips() {
        const pill = create({})
        pill.forceActiveFocus()
        keyClick(Qt.Key_Space)
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Space)
        compare(pill.checked, true)
        compare(pill.toggledCount, 3, "each keystroke counted once")

        pill.checked = false
        compare(pill.focusItem.checked, false)
    }

    function test_a_programmatic_change_does_not_report_a_user_toggle() {
        // The step listens to toggled() to know the user changed their mind.
        // Firing it for a change the step itself made would have it react to
        // its own write.
        const pill = create({})
        pill.checked = true
        compare(pill.toggledCount, 0,
                "setting the property is not a user toggle")

        pill.checked = false
        compare(pill.toggledCount, 0)
    }

    // -- The help link -----------------------------------------------------

    function test_the_help_link_needs_both_a_label_and_a_url() {
        // Half a link is a piece of text that looks clickable and is not.
        // The url half of this guard never worked: a QML url property is a
        // JS object in Qt 6, so `helpUrl !== ""` compares an object to a
        // string and is true however empty the url is.
        const pill = create({})
        verify(!pill.helpLinkItem.visible, "neither given")

        pill.helpLabel = "Learn more"
        verify(!pill.helpLinkItem.visible, "a label with nowhere to go")

        pill.helpLabel = ""
        pill.helpUrl = "https://example.invalid/help"
        verify(!pill.helpLinkItem.visible, "a url with nothing to click")

        pill.helpLabel = "Learn more"
        verify(pill.helpLinkItem.visible, "both, so it is offered")
    }

    // -- Label and accessibility -------------------------------------------

    function test_the_label_is_what_was_asked_for() {
        const pill = create({ text: "Set locale settings" })
        compare(pill.text, "Set locale settings")
    }

    function test_focus_lands_on_the_switch_not_the_wrapper() {
        // Tab order reaches the pill; the keystroke has to reach the switch.
        const pill = create({})
        pill.forceActiveFocus()
        verify(pill.focusItem.activeFocus)
    }

    // -- Activating the help link ------------------------------------------
    //
    // Same five routes as the option rows, and the same reasoning about what
    // shadowing openHelpLink() can and cannot see: tst_option_button.qml has
    // it written out. The pill carries its own copy of the help link, so it
    // needs its own cases -- fixing one component's routes says nothing about
    // the other's.

    Component {
        id: pillWithHelpComponent

        ImOptionPill {
            id: helpPill
            width: 350
            text: "Enable SSH"
            helpLabel: "Learn more about SSH"
            helpUrl: "https://example.invalid/ssh-help"

            property int opened: 0
            property int toggledCount: 0
            onToggled: helpPill.toggledCount++
            function openHelpLink() { helpPill.opened++ }
        }
    }

    function createWithHelp() {
        const p = createTemporaryObject(pillWithHelpComponent, testCase)
        verify(p, "the pill was created")
        verify(p.helpLinkItem.visible, "the link is offered")
        waitForRendering(p)
        return p
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
        const p = createWithHelp()
        const link = p.helpLinkItem

        if (data.how === "click") {
            mouseClick(link)
        } else if (data.how === "key") {
            link.forceActiveFocus()
            verify(link.activeFocus, "the link takes focus")
            keyClick(data.key)
        } else {
            link.Accessible.pressAction()
        }

        compare(p.opened, 1,
                data.tag + " has to open the help link, or a user who reaches "
                + "it that way has a link that does nothing")
    }

    function test_opening_the_help_link_does_not_flip_the_switch() {
        // The label above the link toggles the switch when tapped, and the
        // link is a separate target sitting right under it. Reading the
        // documentation for an option is not agreeing to it -- a pill that
        // came on because the user clicked "learn more" is a setting written
        // to the card that they never asked for.
        const p = createWithHelp()
        compare(p.checked, false, "the option starts off")

        mouseClick(p.helpLinkItem)

        compare(p.opened, 1, "the link opened")
        compare(p.checked, false, "and the option was left alone")
        compare(p.toggledCount, 0, "with nothing reported to the step")
    }
}
