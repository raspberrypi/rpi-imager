/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Tabbing through the Wi-Fi form on a short window.
 *
 * The Wi-Fi step has more fields than fit a small window, so it scrolls. A
 * mouse user drags it; a keyboard user tabs, and tabbing moves focus whether
 * or not the field it lands on is on the screen. So each field asks the scroll
 * view to bring it into view when it takes focus.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "WifiFocusScroll"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property bool wifiConfigured: false
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool sshEnabled: false
        property string networkInfoText: ""
        property int stepWriting: 9
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
        function skipAllCustomisation() { jumpToStep(stepWriting) }
    }

    Component {
        id: stepComponent
        WifiCustomizationStep {
            wizardContainer: fakeContainer
            width: 900
            // Short on purpose: the whole point is a form taller than its
            // window. At the full height everything fits and nothing scrolls,
            // so every assertion below would hold with the handlers removed.
            height: 260
        }
    }

    property var step: null

    function init() {
        fakeContainer.customizationSettings = ({})
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        waitForRendering(step)
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
    }

    function child(name) {
        const c = findChild(step, name)
        verify(c, "found " + name)
        return c
    }

    // The scrolling is done by the Flickable inside the ScrollView, which is
    // an id rather than a named object.
    function findFlickable(item) {
        if (!item)
            return null
        if (item.contentY !== undefined && item.contentHeight !== undefined
                && item.flickableDirection !== undefined)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findFlickable(kids[i])
            if (found)
                return found
        }
        return null
    }

    function flick() {
        const f = findFlickable(step)
        verify(f !== null, "the form is in a Flickable")
        return f
    }

    // Whether a control is inside the visible band of the flickable.
    function isInView(f, item) {
        const pos = item.mapToItem(f.contentItem, 0, 0)
        const top = pos.y
        const bottom = top + item.height
        return top >= f.contentY && bottom <= f.contentY + f.height
    }

    // Put the view where `item` is off screen, if either end of the form does
    // that. Returns false when the item is visible wherever the view sits,
    // which means a case relying on it would prove nothing.
    function scrollAwayFrom(f, item) {
        const ends = [0, Math.max(0, f.contentHeight - f.height)]
        for (let i = 0; i < ends.length; i++) {
            f.contentY = ends[i]
            waitForRendering(step)
            if (!isInView(f, item))
                return true
        }
        return false
    }

    // -- The form really is taller than the window -------------------------

    function test_the_form_scrolls_at_this_size() {
        // Every case below depends on it. If the layout ever gets short
        // enough to fit, they would all pass with the handlers gone.
        const f = flick()
        verify(f.contentHeight > f.height,
               "the form overflows its window: " + f.contentHeight
               + " into " + f.height)
    }

    // -- Focus brings a control into view ----------------------------------

    function test_focusing_a_field_below_the_fold_brings_it_into_view_data() {
        return [
            { tag: "the network name",     name: "wifiSsidField" },
            { tag: "the password",         name: "wifiPasswordField" },
            { tag: "the confirmation",     name: "wifiPasswordConfirmField" },
            { tag: "the hidden-network box", name: "wifiHiddenToggle" }
        ]
    }

    function test_focusing_a_field_below_the_fold_brings_it_into_view(data) {
        const f = flick()
        const field = child(data.name)
        verify(field.visible, data.tag + " is part of the form")

        // The view is put somewhere the field is genuinely off screen before
        // focusing it. Starting at the top of the form is not enough: some of
        // these sit inside the visible band at contentY 0 whatever the window
        // height, and those rows passed with the handler removed because the
        // field was never out of view to begin with.
        //
        // Which end to scroll to depends on where the field is -- the last
        // control cannot be scrolled past, only away from -- so both ends are
        // tried and whichever hides it is used.
        if (!scrollAwayFrom(f, field)) {
            skip(data.tag + " is on screen at both ends of the form, so this "
                 + "row cannot tell the handler being there from it being "
                 + "gone")
            return
        }

        field.forceActiveFocus()
        waitForRendering(step)

        verify(isInView(f, field),
               "tabbing to " + data.tag + " has to bring it on screen, or the "
               + "user types into a field they cannot see")
    }

    function test_focusing_the_last_field_scrolls_down_the_form() {
        // The strong version of the same thing: the control furthest down
        // needs the view to move, so this fails if the view stays put.
        const f = flick()
        const field = child("wifiHiddenToggle")
        f.contentY = 0

        field.forceActiveFocus()
        waitForRendering(step)

        verify(f.contentY > 0,
               "the view scrolled down to reach it; contentY is " + f.contentY)
    }

    // -- And leaves a visible one alone ------------------------------------

    function test_focusing_something_already_in_view_does_not_move_it() {
        // A view that jumps on every tab is as hard to use as one that never
        // moves.
        const f = flick()
        const field = child("wifiSsidField")

        f.contentY = 0
        field.forceActiveFocus()
        waitForRendering(step)
        const settled = f.contentY
        verify(isInView(f, field), "the field is on screen")

        field.forceActiveFocus()
        waitForRendering(step)

        compare(f.contentY, settled,
                "focusing it again leaves the view where it was")
    }

    // -- Without running off either end ------------------------------------

    function test_scrolling_never_goes_above_the_top_of_the_form() {
        const f = flick()
        // Focus the first field from part-way down, so the view has to come
        // back up -- but not past the beginning.
        f.contentY = f.contentHeight - f.height
        child("wifiSsidField").forceActiveFocus()
        waitForRendering(step)

        verify(f.contentY >= 0,
               "the view does not scroll above the form; contentY is "
               + f.contentY)
    }

    function test_scrolling_never_goes_past_the_bottom_of_the_form() {
        const f = flick()
        f.contentY = 0
        child("wifiHiddenToggle").forceActiveFocus()
        waitForRendering(step)

        verify(f.contentY <= f.contentHeight - f.height + 1,
               "the view does not scroll past the end of the form; contentY is "
               + f.contentY + ", limit " + (f.contentHeight - f.height))
    }
    // -- Coming back up to the security tabs -------------------------------
    //
    // The two tabs at the top of the form decide whether a password is asked
    // for at all. Shift-tabbing back up to them from the bottom has to bring
    // them into view, or the user cannot see which of the two is selected --
    // and so cannot tell whether the passphrase they just typed is going to
    // be used or thrown away.

    function test_focusing_a_security_tab_brings_the_top_of_the_form_back_data() {
        return [
            { tag: "the secured-network tab", name: "wifiSecureTab" },
            { tag: "the open-network tab",    name: "wifiOpenTab" }
        ]
    }

    function test_focusing_a_security_tab_brings_the_top_of_the_form_back(data) {
        const f = flick()
        const tab = child(data.name)
        verify(tab.visible, data.tag + " is part of the form")

        f.contentY = f.contentHeight - f.height
        waitForRendering(step)
        if (isInView(f, tab)) {
            skip(data.tag + " is on screen even at the bottom of the form, so "
                 + "this row cannot tell the handler being there from it "
                 + "being gone")
            return
        }

        tab.forceActiveFocus()
        waitForRendering(step)

        verify(isInView(f, tab),
               "focusing " + data.tag + " has to bring it back on screen")
    }
}
