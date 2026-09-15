/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The wizard at sizes nobody designed it for.
 *
 * Every other storm resizes inside a band a few hundred points wide, which
 * is the desktop window and nothing else. The embedded build draws the same
 * steps on a panel, and a user can drag a desktop window to any shape their
 * compositor allows.
 *
 * What breaks at a size is not usually a crash. It is a control squeezed to
 * nothing, or pushed outside the window and left there: still in the focus
 * ring, still announced, and impossible to press. So the storm does not ask
 * whether the wizard survives -- it asks whether what the ring stops on is
 * still on the screen and still has a size.
 *
 * Locked the same way as its neighbours: no drive, no source, no
 * destination, and no key that activates anything.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosGeometry"
    when: windowShown
    width: 1200
    height: 900
    visible: true

    property var wiz: null
    property int rngState: 1
    property bool writeStarted: false
    property bool stormFinished: false

    property int sizesTried: 0
    property int controlsSeen: 0
    property int shapesExamined: 0
    property int stepsAsAsked: 0
    property int shapesWithControls: 0
    property int collapsed: 0
    property int offScreen: 0
    property string offenders: ""

    Connections {
        target: ImageWriterSingleton
        function onPreparationStatusUpdate(msg) { testCase.writeStarted = true }
        function onWriteProgress(now, total)    { testCase.writeStarted = true }
        function onSuccess()                    { testCase.writeStarted = true }
    }

    Component { id: containerComponent; WizardContainer {} }
    Item { id: overlayRoot; anchors.fill: parent }

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

    // main.qml holds the desktop window to 680 by 420, so anything narrower
    // is a shape the application refuses to take and a complaint about one
    // is a complaint about nothing. Trying them said the Next button was off
    // the window at 640 wide, which is true and unreachable.
    //
    // What is left is what can really happen: the panel sizes the embedded
    // build meets, that minimum exactly, and the aspect ratios a compositor
    // will hand a desktop window.
    // Everything in the window is scaled by the same factor as that minimum,
    // the sidebar included, so the shapes are worked out from it rather than
    // written down: at a factor of 1.2 the real floor is 816 by 504. Written
    // down, they described windows the application will not make, and said
    // the Next button was off the right edge -- true, and unreachable.
    readonly property int minimumWidth: Style.scaled(680)
    readonly property int minimumHeight: Style.scaled(420)

    readonly property var shapes: [
        { w: minimumWidth, h: minimumHeight, tag: "the-minimum" },
        { w: minimumWidth, h: Math.round(minimumHeight * 2.4), tag: "narrow-and-tall" },
        { w: Math.round(minimumWidth * 2.4), h: minimumHeight, tag: "wide-and-short" },
        { w: Math.round(minimumWidth * 1.05), h: Math.round(minimumHeight * 1.05),
          tag: "just-above" },
        { w: 1280, h: 800,  tag: "laptop" },
        { w: 1440, h: 900,  tag: "laptop-tall" },
        { w: 1920, h: 1080, tag: "desktop" },
        { w: 2560, h: 1440, tag: "large" },
        { w: 3840, h: 2160, tag: "very-large" }
    ]

    function label(it) {
        if (!it) return "null"
        var n = it.objectName || ""
        return n.length > 0 ? n : String(it).split("(")[0]
    }

    // Walked and measured in one pass, each item while it holds focus.
    //
    // Measuring afterwards was wrong and said so loudly: these steps scroll,
    // and a step scrolls the focused control into view, so by the time the
    // walk came back round the earlier ones had gone off the top again. A
    // control below the fold is not out of reach -- it is reached by being
    // focused, which is what puts it back on screen.
    function walkAndMeasure(shape) {
        var seen = []
        for (var i = 0; i < 50; ++i) {
            var f = wiz.Window.activeFocusItem
            if (f && String(f).indexOf("QQuickRootItem") !== 0) {
                if (seen.indexOf(f) >= 0)
                    break
                seen.push(f)
                measure(f, shape)
            }
            keyClick(Qt.Key_Tab)
            // The scroll that follows focus is not instantaneous.
            waitForRendering(wiz, 500)
        }
        return seen
    }

    function init() {
        TestDrives.clear()
        ImageWriterSingleton.setSrc("")
        ImageWriterSingleton.setDst("")
        verify(!ImageWriterSingleton.readyToWrite(), "the writer is not ready")

        writeStarted = false
        stormFinished = false
        sizesTried = 0
        stepsAsAsked = 0
        controlsSeen = 0
        shapesExamined = 0
        shapesWithControls = 0
        collapsed = 0
        offScreen = 0
        offenders = ""

        wiz = containerComponent.createObject(testCase)
        verify(wiz, "the wizard was built")
        wiz.overlayRootRef = overlayRoot

        failOnWarning(/TypeError/)
        failOnWarning(/ReferenceError/)
        failOnWarning(/is not a function/)
        failOnWarning(/Binding loop/)
        failOnWarning(/Unable to assign/)
    }

    function cleanup() {
        // Read here rather than after the loop: a test function can be
        // abandoned part-way through one and every check after it goes too.
        verify(stormFinished, "the storm reached the end of its loop")
        verify(sizesTried >= 9, "the shapes were tried (" + sizesTried + ")")
        // Per shape rather than in total: which step a shape is paired with
        // is varied by the seed and the steps carry different numbers of
        // controls, so a total moves with the seed. This line has been wrong
        // twice -- a total of thirty, then a floor of three examined shapes
        // -- and a sweep falsified both.
        //
        // Now that a declined step no longer skips a shape, every shape
        // tried is a shape walked, and the question is the one worth asking:
        // did each have something to measure? A step whose controls collapse
        // to nothing at 400x400 fails it, where the earlier two could be got
        // past by being lucky with the seed.
        //
        // shapesExamined == sizesTried is deliberately not asserted. With no
        // continue between the counters it is true by construction, which is
        // a check that can never fail rather than a property. stepsAsAsked
        // is reported for the same reason: how many steps the wizard will
        // show depends on the device and the OS list, not on the window.
        compare(shapesWithControls, sizesTried,
                "every shape had a control to measure (" + shapesWithControls
                + " of " + sizesTried + "; " + stepsAsAsked
                + " were the step asked for)")
        verify(controlsSeen > 5,
               "and there were controls across them (" + controlsSeen + ")")
        compare(collapsed, 0,
                "nothing the ring stops on was squeezed to nothing" + offenders)
        compare(offScreen, 0,
                "and nothing it stops on was pushed off the window" + offenders)
        verify(!writeStarted, "no write began")

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
        return [ { tag: "seed-53", seed: 53 }, { tag: "seed-9311", seed: 9311 } ]
    }

    function test_storm(data) {
        rngState = data.seed

        const steps = [wiz.stepDeviceSelection, wiz.stepOSSelection,
                       wiz.stepStorageSelection, wiz.stepHostnameCustomization,
                       wiz.stepUserCustomization, wiz.stepWifiCustomization,
                       wiz.stepRemoteAccess, wiz.stepIfAndFeatures,
                       wiz.stepDone]

        for (var s = 0; s < shapes.length; ++s) {
            const shape = shapes[s]
            verify(shape.w >= minimumWidth && shape.h >= minimumHeight,
                   shape.tag + " is a shape the window can actually take")
            wiz.width = shape.w
            wiz.height = shape.h
            waitForRendering(wiz, 3000)
            ++sizesTried

            // A different step at each size, so the pairing is not fixed and
            // two seeds cover different ones.
            const step = steps[(s + rndInt(steps.length)) % steps.length]
            wiz.jumpToStep(step)
            wait(30)
            waitForRendering(wiz, 3000)
            if (wiz.currentStep === step)
                ++stepsAsAsked

            // Examine the shape whether or not the wizard would show the
            // step asked for. Which steps it will show depends on the device
            // and the OS list, not on the window; skipping the shape when it
            // declines made the count a draw rather than something the storm
            // controls, and seed 42043 reached two of nine against a floor of
            // three. Declined or not, it is on a step it will show -- the
            // one before, or the one it started on -- and the property here
            // is about the geometry, which a shape nobody looked at says
            // nothing about.
            ++shapesExamined
            examine(shape)
            verify(!writeStarted, "no write began at " + shape.tag)
        }

        stormFinished = true
        console.log("ChaosGeometry seed", data.seed, "shapes", sizesTried,
                    "controls", controlsSeen)
    }

    function measure(it, shape) {
        ++controlsSeen
        var where = " [" + shape.tag + "/" + label(it) + "]"

        if (it.width <= 0 || it.height <= 0) {
            ++collapsed
            if (offenders.indexOf(where) < 0) offenders += where
            return
        }

        // Its middle, in the wizard's own coordinates, while it is the thing
        // the user is on. A control that is still outside the window then is
        // one the step did not bring into view.
        var c = it.mapToItem(wiz, it.width / 2, it.height / 2)
        if (c.x < 0 || c.y < 0 || c.x > wiz.width || c.y > wiz.height) {
            ++offScreen
            if (offenders.indexOf(where) < 0) offenders += where
        }
    }

    function examine(shape) {
        const before = controlsSeen
        walkAndMeasure(shape)
        if (controlsSeen > before)
            ++shapesWithControls
    }
}
