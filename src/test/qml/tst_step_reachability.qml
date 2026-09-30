/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Can a keyboard reach everything a step puts on the screen?
 *
 * The focus ring is not walked, it is composed: each step hands
 * registerFocusGroup a function returning the items it wants in the ring,
 * and WizardStepBase wires KeyNavigation between them. A control added to a
 * step later is not in the ring unless somebody remembered to add it to that
 * list, and nothing says when they did not.
 *
 * That is how the offline Retry came to be unreachable: both list steps
 * registered their list view, the fetch failed, the list was empty, and the
 * ring held nothing at all -- on the one screen whose only control could put
 * it right.
 *
 * So this walks every step, gathers what the step itself declares to be a
 * control, tabs around the ring, and asks whether the two agree. Rows inside
 * a list are the documented exception: a list is one stop and the arrows move
 * within it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "StepReachability"
    when: windowShown
    width: 1000
    height: 760
    visible: true

    property var wiz: null
    property int stepsChecked: 0
    property int controlsExamined: 0
    property int ringItemsSeen: 0

    Component { id: containerComponent; WizardContainer {} }
    Item { id: overlayRoot; anchors.fill: parent }

    // What the tree declares to be something a person operates. Taken from
    // the app's own Accessible.role rather than guessed at from the type, so
    // a control the step says is a button is one this expects to reach.
    readonly property var interactiveRoles: [
        Accessible.Button, Accessible.CheckBox, Accessible.RadioButton,
        Accessible.ComboBox, Accessible.EditableText, Accessible.Link,
        Accessible.Slider, Accessible.PageTab
    ]

    function label(it) {
        if (!it) return "null"
        var n = it.objectName || ""
        return n.length > 0 ? n : String(it).split("(")[0]
    }

    // A step the stack view has left behind keeps its items alive and
    // visible, merely moved aside. Only what is within the wizard's own
    // bounds is on the screen.
    function onScreen(item) {
        var c = item.mapToItem(wiz, item.width / 2, item.height / 2)
        return c.x >= 0 && c.y >= 0 && c.x <= wiz.width && c.y <= wiz.height
    }

    // A list is one stop and the arrows move within it, so nothing inside a
    // row is in the ring -- not the row, and not a control drawn on it. Those
    // are reached by a key on the list instead, which is what
    // tst_os_delegate's "the page can be opened without a mouse" covers. If
    // this exemption is ever what lets a defect through, that is the file to
    // add the case to.
    function insideAList(item) {
        for (var p = item; p; p = p.parent) {
            var view = null
            try { view = p.ListView ? p.ListView.view : null } catch (e) { view = null }
            if (view)
                return true
        }
        return false
    }

    function collectControls(item, out, depth) {
        if (!item || depth > 16 || out.length > 300)
            return
        if (item.visible === false || item.opacity <= 0)
            return
        var role = -1
        try { role = item.Accessible.role } catch (e) { role = -1 }
        if (interactiveRoles.indexOf(role) >= 0 && item.enabled !== false
                && item.width > 0 && item.height > 0 && onScreen(item))
            out.push(item)
        var kids = item.children
        for (var i = 0; kids && i < kids.length; ++i)
            collectControls(kids[i], out, depth + 1)
    }

    function ring() {
        var seen = []
        for (var i = 0; i < 70; ++i) {
            var f = wiz.Window.activeFocusItem
            if (f && String(f).indexOf("QQuickRootItem") !== 0) {
                if (seen.indexOf(f) >= 0)
                    break
                seen.push(f)
            }
            keyClick(Qt.Key_Tab)
            wait(1)
        }
        return seen
    }

    function init() {
        TestDrives.clear()
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
        stepsChecked = 0
        controlsExamined = 0
        ringItemsSeen = 0

        wiz = containerComponent.createObject(testCase)
        verify(wiz, "the wizard was built")
        wiz.overlayRootRef = overlayRoot
        // Unsized, every control in it is degenerate and nothing below means
        // anything.
        wiz.width = testCase.width
        wiz.height = testCase.height
        waitForRendering(wiz)
        verify(wiz.width > 0 && wiz.height > 0, "and has a size")
    }

    function cleanup() {
        if (wiz) {
            wiz.destroy()
            wiz = null
        }
        TestDrives.clear()
    }

    function stepNames() {
        return [
            { tag: "device",     step: wiz.stepDeviceSelection },
            { tag: "os",         step: wiz.stepOSSelection },
            { tag: "storage",    step: wiz.stepStorageSelection },
            { tag: "hostname",   step: wiz.stepHostnameCustomization },
            { tag: "locale",     step: wiz.stepLocaleCustomization },
            { tag: "user",       step: wiz.stepUserCustomization },
            { tag: "wifi",       step: wiz.stepWifiCustomization },
            { tag: "remote",     step: wiz.stepRemoteAccess },
            { tag: "secureboot", step: wiz.stepSecureBootCustomization },
            { tag: "connect",    step: wiz.stepPiConnectCustomization },
            { tag: "features",   step: wiz.stepIfAndFeatures },
            { tag: "done",       step: wiz.stepDone }
        ]
    }

    function test_every_control_on_a_step_is_in_its_ring() {
        var unreachable = []
        const steps = stepNames()

        for (var s = 0; s < steps.length; ++s) {
            wiz.jumpToStep(steps[s].step)
            wait(50)
            waitForRendering(wiz, 2000)
            // A step the wizard declines to show is not this file's business.
            if (wiz.currentStep !== steps[s].step)
                continue
            ++stepsChecked

            var controls = []
            collectControls(wiz, controls, 0)
            var reachable = ring()
            controlsExamined += controls.length
            ringItemsSeen += reachable.length

            for (var c = 0; c < controls.length; ++c)
                if (reachable.indexOf(controls[c]) < 0
                        && !insideAList(controls[c]))
                    unreachable.push(steps[s].tag + "/" + label(controls[c]))
        }

        verify(stepsChecked >= 8, "the wizard showed its steps (" + stepsChecked + ")")
        verify(controlsExamined > 20,
               "with controls on them (" + controlsExamined + ")")
        verify(ringItemsSeen > 20, "and a ring to walk (" + ringItemsSeen + ")")
        compare(unreachable.length, 0,
                "every control is reachable; these were not: "
                + JSON.stringify(unreachable))
    }

    function test_what_a_step_calls_a_tab_stop_is_one() {
        // The roles above are what the tree says is a control. This is the
        // other direction: activeFocusOnTab is a step saying "Tab should
        // stop here", and saying it does not put an item in a ring that is
        // composed from hand-written lists. The information icons say it
        // only while a screen reader is running, which is the state the
        // whole ring is rebuilt for.
        const wasActive = TestAccessibility.isActive()
        TestAccessibility.setActive(true)

        var missing = []
        const steps = stepNames()
        for (var s = 0; s < steps.length; ++s) {
            wiz.jumpToStep(steps[s].step)
            wait(50)
            waitForRendering(wiz, 2000)
            if (wiz.currentStep !== steps[s].step)
                continue
            ++stepsChecked

            var declared = []
            collectTabStops(wiz, declared, 0)
            var reachable = ring()
            controlsExamined += declared.length
            ringItemsSeen += reachable.length

            for (var d = 0; d < declared.length; ++d)
                if (!reachedBySelfOrChild(declared[d], reachable))
                    missing.push(steps[s].tag + "/" + label(declared[d]))
        }

        TestAccessibility.setActive(wasActive)

        verify(stepsChecked >= 8, "the wizard showed its steps (" + stepsChecked + ")")
        verify(controlsExamined > 20,
               "and declared stops on them (" + controlsExamined + ")")
        compare(missing.length, 0,
                "every declared stop is in the ring; these were not: "
                + JSON.stringify(missing))
    }

    // A wrapper that hands its focus to the field inside it -- which is what
    // ImPasswordField does, deliberately, so that one control does not
    // present two accessible interfaces -- is reached when the field is.
    function reachedBySelfOrChild(item, reachable) {
        if (!item)
            return false
        if (reachable.indexOf(item) >= 0)
            return true
        var kids = item.children
        for (var i = 0; kids && i < kids.length; ++i)
            if (reachedBySelfOrChild(kids[i], reachable))
                return true
        return false
    }

    function collectTabStops(item, out, depth) {
        if (!item || depth > 16 || out.length > 300)
            return
        if (item.visible === false || item.opacity <= 0)
            return
        if (item.activeFocusOnTab === true && item.enabled !== false
                && item.width > 0 && item.height > 0 && onScreen(item))
            out.push(item)
        var kids = item.children
        for (var i = 0; kids && i < kids.length; ++i)
            collectTabStops(kids[i], out, depth + 1)
    }

    function test_everything_the_ring_stops_on_announces_itself() {
        // A stop that says nothing is a stop a screen reader user cannot
        // identify. Containers that merely catch focus are not asked --
        // activeFocusOnTab is how a step says an item is a stop it meant.
        var silent = []
        const steps = stepNames()

        for (var s = 0; s < steps.length; ++s) {
            wiz.jumpToStep(steps[s].step)
            wait(50)
            waitForRendering(wiz, 2000)
            if (wiz.currentStep !== steps[s].step)
                continue
            ++stepsChecked

            var reachable = ring()
            ringItemsSeen += reachable.length
            for (var r = 0; r < reachable.length; ++r) {
                var it = reachable[r]
                if (it.activeFocusOnTab !== true)
                    continue
                var name = ""
                try { name = it.Accessible.name || "" } catch (e) {}
                if (name.length === 0)
                    silent.push(steps[s].tag + "/" + label(it))
            }
        }

        verify(stepsChecked >= 8, "the wizard showed its steps (" + stepsChecked + ")")
        verify(ringItemsSeen > 20, "and a ring to walk (" + ringItemsSeen + ")")
        compare(silent.length, 0,
                "every stop announces itself; these did not: "
                + JSON.stringify(silent))
    }
}
