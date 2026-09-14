/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Walking the wizard with nothing but the keyboard.
 *
 * The other storms mix clicks and keys, which is not how a keyboard user
 * arrives anywhere: a click can put focus on something Tab never reaches,
 * and the trajectory a click opens up hides what the Tab ring actually does.
 * This one never touches the mouse.
 *
 * What it asks is not that the wizard survives -- the other storms cover
 * that -- but that focus is always somewhere a person could use. A control
 * that is invisible, disabled or zero-sized and still in the Tab ring is a
 * dead stop: the user presses Tab, the ring appears to vanish, and there is
 * nothing on screen to press. Several controls here are conditionally
 * visible, and activeFocusOnTab is bound to whether a screen reader is
 * running, so the ring is rebuilt constantly.
 *
 * No key that activates anything is ever sent -- no Return, no Space -- so
 * nothing can be confirmed by accident. Steps are moved between directly.
 * With no drive, no source and no destination, there is nothing to write to
 * even if something were pressed.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosKeyboard"
    when: windowShown
    width: 900
    height: 700
    visible: true

    property var wiz: null
    property int rngState: 1
    property bool writeStarted: false
    property bool stormFinished: false
    property bool startingAccessibility: false

    property int keysSent: 0
    property int focusReadings: 0
    property int distinctFocused: 0
    property int invisibleFocused: 0
    property int disabledFocused: 0
    property int emptyFocused: 0
    property string offenders: ""

    Connections {
        target: ImageWriterSingleton
        function onPreparationStatusUpdate(msg) { testCase.writeStarted = true }
        function onWriteProgress(now, total)    { testCase.writeStarted = true }
        function onSuccess()                    { testCase.writeStarted = true }
    }

    Component {
        id: containerComponent
        WizardContainer {}
    }

    Item {
        id: overlayRoot
        anchors.fill: parent
    }

    // Math.imul, not *: the product reaches 2.4e18, which is 263 times
    // past the largest integer a double holds exactly, so the multiply
    // rounded before the mask ever ran. The sequence stopped being the
    // LCG it was written as at the second draw, and consecutive draws
    // correlated -- eleven jumps in a row chose the same destination of
    // twelve on one seed, which is what found this.
    function rnd() {
        rngState = (Math.imul(rngState, 1103515245) + 12345) & 0x7fffffff
        return rngState / 0x7fffffff
    }

    function rndInt(n) { return n > 0 ? Math.floor(rnd() * n) % n : 0 }

    // Navigation only. Return and Space activate whatever holds focus, and
    // one of the things that can hold focus confirms a write.
    readonly property var navKeys: [
        Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Down, Qt.Key_Up,
        Qt.Key_Left, Qt.Key_Right, Qt.Key_Home, Qt.Key_End,
        Qt.Key_PageDown, Qt.Key_PageUp, Qt.Key_Escape
    ]

    function init() {
        startingAccessibility = TestAccessibility.isActive()
        TestDrives.clear()
        compare(TestDrives.count(), 0, "no drive while the ring is walked")
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
        verify(!ImageWriterSingleton.readyToWrite(), "the writer is not ready")

        writeStarted = false
        stormFinished = false
        keysSent = 0
        focusReadings = 0
        distinctFocused = 0
        invisibleFocused = 0
        disabledFocused = 0
        emptyFocused = 0
        offenders = ""

        failOnWarning(/TypeError/)
        failOnWarning(/ReferenceError/)
        failOnWarning(/Binding loop/)
        failOnWarning(/is not a function/)
        failOnWarning(/Unable to assign/)
    }

    function cleanup() {
        TestAccessibility.setActive(startingAccessibility)

        // Read here rather than after the loop: a test function can be
        // abandoned part-way through one, and every check written after it
        // goes with it, leaving a pass that stands for nothing.
        verify(stormFinished, "the storm reached the end of its loop")
        verify(keysSent > 200, "keys were sent (" + keysSent + ")")
        verify(focusReadings > 50,
               "and something held focus to look at (" + focusReadings + ")")
        verify(distinctFocused > 5,
               "the ring moved between controls (" + distinctFocused + ")")

        compare(invisibleFocused, 0,
                "nothing invisible held focus" + offenders)
        compare(disabledFocused, 0,
                "nothing disabled held focus" + offenders)
        compare(emptyFocused, 0,
                "nothing of no size held focus" + offenders)
        verify(!writeStarted, "and no write began")

        TestDrives.clear()
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
    }

    function cleanupTestCase() {
        if (wiz) {
            wiz.destroy()
            wiz = null
        }
        TestDrives.clear()
    }

    function test_storm_data() {
        var override = TestEnv.value("RPI_CHAOS_SEED")
        if (override.length > 0)
            return [ { tag: "seed-" + override, seed: parseInt(override) } ]
        return [ { tag: "seed-41", seed: 41 }, { tag: "seed-8221", seed: 8221 } ]
    }

    function test_storm(data) {
        rngState = data.seed

        if (!wiz)
            wiz = containerComponent.createObject(testCase)
        verify(wiz !== null, "the wizard was built")
        wiz.overlayRootRef = overlayRoot

        // Without this the wizard is nothing by nothing, every control in it
        // is degenerate, and the checks below fire on all of them -- a storm
        // walking a ring that is not on screen proves nothing either way.
        wiz.width = testCase.width
        wiz.height = testCase.height
        waitForRendering(wiz)
        verify(wiz.width > 0 && wiz.height > 0, "the wizard has a size to walk")

        var seen = ({})   // no prototype: an objectName of "constructor" is a name
        seen = Object.create(null)

        // Every step, and the writing one on the way past rather than sat on.
        const steps = [wiz.stepDeviceSelection, wiz.stepOSSelection,
                       wiz.stepStorageSelection, wiz.stepHostnameCustomization,
                       wiz.stepLocaleCustomization, wiz.stepUserCustomization,
                       wiz.stepWifiCustomization, wiz.stepRemoteAccess,
                       wiz.stepSecureBootCustomization,
                       wiz.stepPiConnectCustomization, wiz.stepIfAndFeatures,
                       wiz.stepDone]

        for (var s = 0; s < steps.length; ++s) {
            wiz.jumpToStep(steps[s])
            wait(1)

            // The ring is rebuilt when a screen reader appears, because
            // activeFocusOnTab is bound to it. Both states get walked.
            TestAccessibility.setActive(s % 2 === 0)
            wait(1)

            for (var k = 0; k < 30; ++k) {
                if (k % 10 === 9) {
                    wiz.width = 700 + rndInt(500)
                    wiz.height = 460 + rndInt(300)
                    waitForRendering(wiz, 500)
                }

                keyClick(navKeys[rndInt(navKeys.length)])
                ++keysSent

                // A step left behind takes its controls with it; keep the
                // walk on the screen it is meant to be walking.
                if (wiz.currentStep !== steps[s]) {
                    wiz.jumpToStep(steps[s])
                    wait(1)
                }

                examineFocus(seen)
                verify(!writeStarted, "no write began")
                compare(TestDrives.count(), 0, "the drive list stayed empty")
            }
        }

        distinctFocused = Object.keys(seen).length
        stormFinished = true

        console.log("ChaosKeyboard seed", data.seed, "keys", keysSent,
                    "focus readings", focusReadings,
                    "distinct", distinctFocused)
    }

    // Whatever holds focus has to be something a person could act on. In
    // Qt Quick both visible and enabled are the effective values, so an
    // ancestor hiding or disabling a control shows up here without walking
    // the tree.
    //
    // Tab skips what is invisible or disabled, so those two do not catch a
    // ring built wrongly -- they catch the other shape, a control that loses
    // its visibility or its enabled state while already holding focus, which
    // the resizes and step changes above produce constantly. The size check
    // is the one with teeth: left unsized, this wizard reported osList,
    // hostnameField and storageDeviceList on 84 readings across two seeds.
    function examineFocus(seen) {
        var focused = wiz.Window.activeFocusItem
        if (!focused)
            return
        // The window's root item holds focus whenever nothing else does,
        // which is an ordinary state rather than a control to check.
        if (String(focused).indexOf("QQuickRootItem") === 0)
            return

        ++focusReadings
        seen[(focused.objectName || "") + "/" + String(focused)] = true

        var where = " [" + (focused.objectName || String(focused)) + "]"
        if (focused.visible === false) {
            ++invisibleFocused
            if (offenders.indexOf(where) < 0) offenders += where
        }
        if (focused.enabled === false) {
            ++disabledFocused
            if (offenders.indexOf(where) < 0) offenders += where
        }
        // A control has no size until it has been laid out, and a step
        // arriving takes a frame or two to get there. Measured once, that
        // reads as a dead stop on perfectly ordinary controls -- the list
        // views and the hostname field among them. Only a control still
        // empty after the layout has settled is one a user could land on
        // and find nothing.
        if (focused.width !== undefined && focused.height !== undefined
                && (focused.width <= 0 || focused.height <= 0)) {
            wait(1)
            waitForRendering(wiz, 500)
            if (focused && focused.width !== undefined
                    && (focused.width <= 0 || focused.height <= 0)
                    && wiz.Window.activeFocusItem === focused) {
                ++emptyFocused
                if (offenders.indexOf(where) < 0) offenders += where
            }
        }
    }
}
