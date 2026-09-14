/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The second card, written by a wizard that has already written one.
 *
 * "Write Another" is the only route in the application that runs a whole
 * write a second time through objects the first write already used. It
 * skips every customisation step and replays the payload staged for the
 * previous write, so the second card is produced by state nobody re-entered.
 * That is the shape of defect a single-cycle test cannot see: the memset in
 * PerformanceStats leaked nothing on a first write and was found by the one
 * case in the suite that performed a second.
 *
 * No storm covered this flow. The eleven others drive a wizard that has
 * never completed anything.
 *
 * Safe by construction rather than by care, on the same three locks the
 * other storms use: the drive list is emptied and its poller stopped, the
 * writer is given no destination so startWrite() would refuse, and a
 * tripwire on the writer's own signals fails the run if anything resembling
 * a write begins regardless. Entering the writing step does not start one --
 * startWrite() sits behind an activation handler -- and controls whose text
 * or name mentions writing are never chosen as targets.
 *
 * What it asserts is what resetToWriteStep() and nextStep() themselves
 * state: the reset lands on storage selection with the flag raised, and
 * advancing from there reaches the writing step with the flag lowered.
 * Everything else is the standard storm property -- the wizard survives,
 * stays on a real step, and logs no QML error.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosWriteAnother"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Component {
        id: containerComponent
        WizardContainer {}
    }

    // The wizard puts its popups in an overlay the application window owns;
    // a container built on its own has nowhere to put them.
    Item {
        id: overlayRoot
        anchors.fill: parent
    }

    property var wiz: null
    property int rngState: 1

    // Checked in cleanup() rather than after the loop: a test function can be
    // abandoned part-way and every check written after the loop goes with it,
    // leaving a pass that stands for nothing.
    property bool stormFinished: false

    // Tripped if anything resembling a write ever begins.
    property bool writeStarted: false

    // Totalled and checked once at the end. A run where one of these stopped
    // firing would otherwise go on passing while asking one fewer question.
    property int resetsDone: 0
    property int replaysCompleted: 0
    property int backwardsDone: 0
    property int fullResetsDone: 0

    Connections {
        target: ImageWriterSingleton
        function onPreparationStatusUpdate(msg) { testCase.writeStarted = true }
        function onWriteProgress(now, total)    { testCase.writeStarted = true }
        function onSuccess()                    { testCase.writeStarted = true }
    }

    // A small LCG rather than Math.random(): the seed is what makes a failure
    // reproducible, and Math.random cannot be seeded.
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

    function rndInt(n) {
        return n > 0 ? Math.floor(rnd() * n) % n : 0;
    }

    function collectTargets(item, out, depth) {
        if (!item || depth > 12 || out.length > 300)
            return;
        if (item.visible === false)
            return;
        if (item.width > 0 && item.height > 0)
            out.push(item);
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            collectTargets(kids[i], out, depth + 1);
    }

    // A control that would start writing is never a valid target, whatever
    // the drive list says. The list is emptied as well; this is the second
    // lock on the same door.
    function isDestructive(item) {
        var t = "";
        if (item.text !== undefined && typeof item.text === "string")
            t = item.text.toLowerCase();
        var n = (item.objectName || "").toLowerCase();
        return t.indexOf("write") >= 0 || t.indexOf("erase") >= 0
            || t.indexOf("format") >= 0 || n.indexOf("write") >= 0
            || n.indexOf("erase") >= 0 || n.indexOf("format") >= 0;
    }

    function init() {
        TestDrives.clear();
        compare(TestDrives.count(), 0, "the storm must have no drive to write to");

        ImageWriterSingleton.setDst("");
        verify(!ImageWriterSingleton.readyToWrite(),
               "the writer must not be ready to write anything");

        writeStarted = false;
        stormFinished = false;

        if (!wiz)
            wiz = containerComponent.createObject(testCase);
        verify(wiz !== null, "the wizard container was built");
        wiz.overlayRootRef = overlayRoot;

        // A QML error does not stop a running application: the binding is
        // abandoned and the log carries a line nobody reads.
        failOnWarning(/TypeError/);
        failOnWarning(/Binding loop/);
        failOnWarning(/is not a function/);
        failOnWarning(/Unable to assign/);
        failOnWarning(/ReferenceError/);
    }

    function cleanup() {
        verify(stormFinished, "the storm reached the end of its loop");
    }

    function test_storm_data() {
        var override = TestEnv.value("RPI_CHAOS_SEED")
        if (override.length > 0)
            return [ { tag: "seed-" + override, seed: parseInt(override) } ]
        return [ { tag: "seed-31", seed: 31 }, { tag: "seed-8191", seed: 8191 } ]
    }

    function test_storm(data) {
        rngState = data.seed;

        const keys = [Qt.Key_Down, Qt.Key_Up, Qt.Key_Tab, Qt.Key_Backtab,
                      Qt.Key_Left, Qt.Key_Right, Qt.Key_Escape, Qt.Key_Space];

        // Measured on an idle machine: about 29ms an iteration, near enough
        // linear, so a thirty-seed sweep of this storm costs roughly two
        // minutes on the shipping build. Timings taken while the machine was
        // busy said the opposite and were worthless -- they were reading the
        // load, not the loop.
        for (var i = 0; i < 120; ++i) {
            var action = rndInt(11);

            if (action < 3) {
                // The flow under test. Both halves of its contract are stated
                // unconditionally by the functions themselves, so both are
                // checked rather than merely survived.
                wiz.resetToWriteStep();
                resetsDone++;
                compare(wiz.currentStep, wiz.stepStorageSelection,
                        "resetToWriteStep lands on storage selection");
                verify(wiz.writeAnotherMode,
                       "resetToWriteStep raises the replay flag");

                // Advancing from storage selection is the replay: it reaches
                // the writing step directly and lowers the flag again. None of
                // the step-skipping adjustments can fire on that index.
                if (rndInt(2) === 0) {
                    wiz.nextStep();
                    replaysCompleted++;
                    compare(wiz.currentStep, wiz.stepWriting,
                            "the replay reaches the writing step");
                    verify(!wiz.writeAnotherMode,
                           "the replay flag is consumed, not left raised");
                }
            } else if (action < 5) {
                wiz.nextStep();
            } else if (action < 7) {
                var before = wiz.currentStep;
                wiz.previousStep();
                if (wiz.currentStep !== before)
                    backwardsDone++;
            } else if (action < 8) {
                // The other reset, and the one the repository dialog calls.
                // Reached here while the replay flag may be raised, which is
                // the interesting order: a full reset has to take the flag
                // down with everything else, or the next advance from storage
                // selection would skip the customisation steps for an OS list
                // that has just been replaced.
                wiz.resetWizard();
                fullResetsDone++;
                verify(!wiz.writeAnotherMode,
                       "a full reset lowers the replay flag");
                verify(wiz.currentStep === 0 || wiz.currentStep === 1,
                       "a full reset returns to the start (" + wiz.currentStep + ")");
            } else if (action < 10) {
                var targets = [];
                collectTargets(wiz, targets, 0);
                if (targets.length > 0) {
                    var t = targets[rndInt(targets.length)];
                    if (!isDestructive(t))
                        mouseClick(t, Math.floor(t.width / 2), Math.floor(t.height / 2));
                }
            } else {
                keyClick(keys[rndInt(keys.length)]);
            }

            // Held every iteration rather than once at the end, so a failure
            // names the action that caused it.
            verify(wiz.currentStep >= 0 && wiz.currentStep < wiz.totalSteps,
                   "the wizard is on a real step (" + wiz.currentStep + ")");
            verify(!writeStarted, "no write began at iteration " + i);
            verify(!ImageWriterSingleton.readyToWrite(),
                   "the writer never became ready to write");
        }

        // A deterministic tail, so every seed exercises both resets rather
        // than relying on the walk to have drawn them. A one-in-eleven action
        // is missed by roughly one seed in fifty over forty draws, and the
        // counters below are totalled across the file, so the other seed
        // could satisfy them. This makes each seed answer for itself.
        wiz.resetToWriteStep();
        resetsDone++;
        compare(wiz.currentStep, wiz.stepStorageSelection,
                "resetToWriteStep lands on storage selection");
        verify(wiz.writeAnotherMode, "resetToWriteStep raises the replay flag");

        wiz.nextStep();
        replaysCompleted++;
        compare(wiz.currentStep, wiz.stepWriting,
                "the replay reaches the writing step");
        verify(!wiz.writeAnotherMode, "the replay flag is consumed");

        wiz.resetWizard();
        fullResetsDone++;
        verify(!wiz.writeAnotherMode, "a full reset lowers the replay flag");
        verify(wiz.currentStep === 0 || wiz.currentStep === 1,
               "a full reset returns to the start (" + wiz.currentStep + ")");

        verify(!writeStarted, "no write began");

        stormFinished = true;
    }

    function test_zzz_dimensions_fired() {
        // Totals across the whole file, so a run that quietly stopped
        // exercising the replay cannot pass as one that did.
        verify(resetsDone > 0, "the write-another reset fired (" + resetsDone + ")");
        verify(replaysCompleted > 0,
               "the replay advanced to the writing step (" + replaysCompleted + ")");
        verify(fullResetsDone > 0,
               "the full reset fired (" + fullResetsDone + ")");
        stormFinished = true;
    }
}
