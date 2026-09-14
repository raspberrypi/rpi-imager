/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The wizard being taken away while it is still busy.
 *
 * The other storms keep one container and destroy a quiescent wizard once,
 * at the end. This builds one, storms it, destroys it without waiting and
 * pumps the event loop -- sixty times over three seeds. Under a sanitiser
 * that is the destruction order of every step, Loader and Connections.
 *
 * Written for the deferred-callback defects and does not reach them: a
 * probe destroying the container before its Qt.callLater ran fired fifty of
 * fifty.
 *
 * Same three locks: no device within reach, no write begun.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosTeardown"
    when: windowShown
    width: 800
    height: 600
    visible: true

    property int rngState: 1
    property bool writeStarted: false
    property bool stormFinished: false
    property int roundsRun: 0
    property int clicksLanded: 0

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

    // The wizard puts its popups in an overlay the application window owns,
    // and a container built on its own has nowhere to put them. This stays
    // alive across every round, so it is also the thing most likely to still
    // be holding a child of a container that has just been destroyed.
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
        rngState = (Math.imul(rngState, 1103515245) + 12345) & 0x7fffffff;
        return rngState / 0x7fffffff;
    }

    function rndInt(n) { return n > 0 ? Math.floor(rnd() * n) % n : 0; }

    function init() {
        TestDrives.clear();
        compare(TestDrives.count(), 0, "no drive while the wizard is torn down");
        ImageWriterSingleton.setDst("");
        verify(!ImageWriterSingleton.readyToWrite(), "the writer is not ready");
        writeStarted = false;
        stormFinished = false;
        roundsRun = 0;
        clicksLanded = 0;

        // Teardown failures are warnings, not crashes: Qt reports them and
        // carries on, which is how this class of defect survives a run that
        // looks green.
        failOnWarning(/TypeError/);
        failOnWarning(/ReferenceError/);
        failOnWarning(/is not a function/);
        failOnWarning(/Binding loop/);
        failOnWarning(/Unable to assign/);
    }

    function cleanup() {
        // Read here rather than after the loop: a test function can be
        // abandoned part-way and every check written after its loop goes
        // with it, leaving a pass that stands for nothing. cleanup() runs
        // either way.
        verify(stormFinished, "the storm reached the end of its loop");
        verify(roundsRun === 20, "every round ran (" + roundsRun + " of 20)");
        verify(clicksLanded > 10, "the rounds landed clicks (" + clicksLanded + ")");
        verify(!writeStarted, "and no write began");
        TestDrives.clear();
    }

    function test_teardown_data() {
        var override = TestEnv.value("RPI_CHAOS_SEED");
        if (override.length > 0)
            return [ { tag: "seed-" + override, seed: parseInt(override) } ];
        return [ { tag: "seed-11", seed: 11 },
                 { tag: "seed-1013", seed: 1013 },
                 { tag: "seed-32771", seed: 32771 } ];
    }

    function test_teardown(data) {
        rngState = data.seed;

        var keys = [Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Return, Qt.Key_Space,
                    Qt.Key_Escape, Qt.Key_Down, Qt.Key_Up, Qt.Key_Left,
                    Qt.Key_Right];

        // The same set the wizard storm uses, and for the same reason: the
        // two writing screens are left out because their controls call
        // startWrite(), ejectDrive() and reboot(), and reboot() is live on
        // the boards these tests run on.
        var reachableSteps = [-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

        var destroyedMidFlight = 0;

        for (var round = 0; round < 20; ++round) {
            var wiz = containerComponent.createObject(testCase);
            verify(wiz !== null, "the wizard was built for round " + round);
            wiz.overlayRootRef = overlayRoot;

            var dest = reachableSteps[rndInt(reachableSteps.length)];
            wiz.jumpToStep(dest);

            var actions = 3 + rndInt(8);
            for (var a = 0; a < actions; ++a) {
                var targets = [];
                collect(wiz, targets, 0);
                if (targets.length === 0)
                    break;

                var choice = rndInt(10);
                if (choice < 5) {
                    var it = targets[rndInt(targets.length)];
                    if (!isDestructive(it)) {
                        mouseClick(it, rndInt(Math.max(1, Math.floor(it.width))),
                                       rndInt(Math.max(1, Math.floor(it.height))));
                        ++clicksLanded;
                    }
                } else if (choice < 8) {
                    keyClick(keys[rndInt(keys.length)]);
                } else {
                    var f = targets[rndInt(targets.length)];
                    if (f.forceActiveFocus !== undefined && !isDestructive(f))
                        f.forceActiveFocus();
                }
            }

            // A jump first, half the time: a step transition schedules more
            // deferred work than anything else the storm does, so this is
            // the state that has the most in flight when the container is
            // taken away.
            if (rndInt(2) === 0) {
                wiz.jumpToStep(reachableSteps[rndInt(reachableSteps.length)]);
                ++destroyedMidFlight;
            }

            wiz.destroy();
            wiz = null;

            // destroy() only schedules, so the pumps are what actually run
            // the teardown and everything that was queued behind it. Two of
            // them: the first drains what was pending, the second runs what
            // the teardown itself scheduled.
            wait(1);
            wait(1);

            verify(!writeStarted, "no write began in round " + round);
            compare(TestDrives.count(), 0, "the drive list stayed empty");
            verify(!ImageWriterSingleton.readyToWrite(), "the writer stayed unready");
            roundsRun = round + 1;
        }

        stormFinished = true;

        console.log("ChaosTeardown seed", data.seed, "rounds", roundsRun,
                    "clicks", clicksLanded,
                    "destroyed mid-transition", destroyedMidFlight);
    }

    function collect(item, out, depth) {
        if (!item || depth > 12 || out.length > 250)
            return;
        if (item.visible === false)
            return;
        if (item.width > 0 && item.height > 0)
            out.push(item);
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            collect(kids[i], out, depth + 1);
    }

    function isDestructive(item) {
        var t = (item.text !== undefined && typeof item.text === "string")
                ? item.text.toLowerCase() : "";
        var n = (item.objectName || "").toLowerCase();
        return t.indexOf("write") >= 0 || t.indexOf("erase") >= 0
            || t.indexOf("format") >= 0 || n.indexOf("write") >= 0
            || n.indexOf("erase") >= 0 || n.indexOf("format") >= 0;
    }
}
