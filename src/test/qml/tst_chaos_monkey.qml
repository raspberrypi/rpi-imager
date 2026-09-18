/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Random interaction against the wizard, to find what ordered tests do not.
 *
 * Every other QML case drives one screen along a path somebody thought of.
 * This fires clicks, keys and text at whatever is on screen in an order
 * nobody intended: a dialog opened from a step already leaving, Escape
 * during a transition, typing into a field just hidden. The property is
 * that the wizard survives and stays usable.
 *
 * The sequence is seeded and the seed printed, so a failure can be
 * replayed.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosMonkey"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Component {
        id: containerComponent
        WizardContainer {}
    }

    // The wizard puts its popups in an overlay the application window owns.
    // A container built on its own has nowhere to put them, and the steps
    // that ask fall back to a window property that does not exist here --
    // which is a warning from the harness, not from the code under test.
    // Giving it somewhere real to point removes the noise and exercises the
    // path production takes.
    Item {
        id: overlayRoot
        anchors.fill: parent
    }

    property var wiz: null
    property int rngState: 1

    // Noted before the storm runs, because changeLanguage() refuses a name
    // it does not know and refuses the one already installed: the name has
    // to be the one the writer itself reports. It reports nothing when the
    // system locale matched no translation, and there is no way to take a
    // translator back off -- English is the source language, so installing
    // it is the closest thing to undoing the storm's last pick.
    property string startingLanguage: ""
    // Totalled across the file and checked once at the end, rather than per
    // case: a case can end early with none of these, but a run where the
    // dimension stopped firing altogether would otherwise go on passing
    // while testing one fewer thing than it says it does.
    property int languagePicksTotal: 0
    property int shapeChangesTotal: 0
    // Every distinct value getLastCustomizationStep() returned across the
    // file. More than one of them is the proof that flipping the four
    // capability flags moved the wizard's shape rather than just the flags.
    property var lastStepsSeen: ({})
    property int accessibilityFlipsTotal: 0
    property var screenReaderSeen: ({})
    // Restored at the end: QAccessible::isActive() is process-wide, and the
    // files that stand deliberately on one side of it run after this one.
    property bool startingAccessibility: false

    // Set when the storm's loop is left, and checked in cleanup() rather
    // than after the loop. A test function can be abandoned part-way -- a
    // step that spends long enough inside a nested event loop is enough --
    // and every check written after the loop goes with it, leaving a pass
    // that stands for nothing. cleanup() still runs.
    property bool stormFinished: false


    // Tripped if anything resembling a write ever begins. Random keys can
    // activate a focused control without a mouse click, so filtering what
    // gets clicked is not on its own enough.
    property bool writeStarted: false

    Connections {
        target: ImageWriterSingleton
        function onPreparationStatusUpdate(msg) { testCase.writeStarted = true }
        function onWriteProgress(now, total)    { testCase.writeStarted = true }
        function onSuccess()                    { testCase.writeStarted = true }
    }

    // A small LCG rather than Math.random(): the seed is what makes a
    // failure reproducible, and Math.random cannot be seeded.
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

    // Anything on screen with an area to aim at. Deliberately not filtered
    // to "controls": hitting decorations and containers is the point.
    function collectTargets(item, out, depth) {
        if (!item || depth > 12 || out.length > 400)
            return;
        if (item.visible === false)
            return;
        if (item.width > 0 && item.height > 0)
            out.push(item);
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            collectTargets(kids[i], out, depth + 1);
    }

    // A control that would start writing to a device is never a valid
    // target, whatever the drive list says. The list is emptied below as
    // well; this is the second lock on the same door.
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
        // Three independent locks, because the monkey presses Enter too.
        //
        // 1. No drive in the list, and the poller stopped, so the machine's
        //    own disks cannot reappear in it.
        TestDrives.clear();
        compare(TestDrives.count(), 0, "the monkey must have no drive to write to");

        // 2. No destination on the writer. startWrite() refuses without one,
        //    so even a keyboard activation of a write control does nothing.
        ImageWriterSingleton.setDst("");
        verify(!ImageWriterSingleton.readyToWrite(),
               "the writer must not be ready to write anything");

        // 3. And if a write somehow began anyway, the Connections above
        //    record it and the run fails on the next check.
        writeStarted = false;

        if (!wiz)
            wiz = containerComponent.createObject(testCase);
        verify(wiz !== null, "the wizard container was built");
        wiz.overlayRootRef = overlayRoot;

        // A QML error does not stop a running application: the binding is
        // abandoned, the property keeps its old value and the log carries a
        // line nobody reads. Under a monkey that is exactly what gets hit,
        // so the warnings are made failures rather than left to a sweep.
        // The null-focus TypeError in OSSelectionStep was found this way.
        failOnWarning(/TypeError/);
        failOnWarning(/Binding loop/);
        failOnWarning(/is not a function/);
        failOnWarning(/Unable to assign/);
        failOnWarning(/ReferenceError/);
    }

    function initTestCase() {
        startingLanguage = ImageWriterSingleton.getCurrentLanguage();
        startingAccessibility = TestAccessibility.isActive();
    }

    function cleanup() {
        verify(stormFinished, "the storm reached the end of its loop");
    }

    // Set the flag and wait for the polled value to catch up, so what is
    // recorded is what the wizard saw rather than what it was about to see.
    function observeScreenReader(on) {
        TestAccessibility.setActive(on);
        tryVerify(function () {
            return ImageWriterSingleton.screenReaderActive === on;
        }, 3000, "the wizard noticed the screen reader going "
                 + (on ? "on" : "off"));
        screenReaderSeen[String(ImageWriterSingleton.screenReaderActive)] = true;
    }

    function cleanupTestCase() {
        verify(languagePicksTotal > 0,
               "the storm changed language at least once");
        TestAccessibility.setActive(startingAccessibility);
        verify(accessibilityFlipsTotal > 0,
               "the storm turned accessibility on and off");
        var srValues = Object.keys(screenReaderSeen);
        verify(srValues.length > 1,
               "and the wizard saw it both ways (" + JSON.stringify(srValues) + ")");
        verify(shapeChangesTotal > 0,
               "and changed which customisation steps exist at least once");
        var shapes = Object.keys(lastStepsSeen);
        verify(shapes.length > 1,
               "which moved the last customisation step (" +
               JSON.stringify(shapes) + ")");
        // The language is process-wide, and every case in this binary shares
        // one ImageWriter. Leaving the storm's last pick installed would run
        // the rest of the run translated, and the string comparisons in the
        // other files would start failing for no reason they could explain.
        ImageWriterSingleton.changeLanguage(
            startingLanguage.length > 0 ? startingLanguage : "English");
        if (wiz) {
            wiz.destroy();
            wiz = null;
        }
        TestDrives.clear();
    }

    function test_random_interaction_data() {
        // Several seeds rather than one long run: independent sequences
        // reach different corners, and a failing one names itself.
        //
        // RPI_CHAOS_SEED replaces the set with one run, so a seed printed by
        // a failure can be replayed on its own.
        var override = TestEnv.value("RPI_CHAOS_SEED");
        if (override.length > 0)
            return [ { tag: "seed-" + override, seed: parseInt(override) } ];

        return [
            { tag: "seed-1", seed: 1 },
            { tag: "seed-7919", seed: 7919 },
            { tag: "seed-104729", seed: 104729 },
            { tag: "seed-1299709", seed: 1299709 },
            { tag: "seed-15485863", seed: 15485863 },
        ];
    }

    function test_random_interaction(data) {
        rngState = data.seed;
        console.log("ChaosMonkey seed", data.seed);

        // Counted, not assumed: a run that found nothing to click would
        // pass every invariant below while proving nothing at all.
        var clicks = 0;
        var maxTargets = 0;
        var jumps = 0;
        var rescues = 0;
        var languagePicks = 0;
        var shapeChanges = 0;
        var accessibilityFlips = 0;

        // Controls that took focus with nothing for a screen reader to say.
        // Recorded rather than asserted: the number is unknown, and a check
        // that fires on every seed teaches nothing about which ones matter.
        var namelessFocused = ({});

        var stepsSeen = ({});

        // The wizard is shown both sides of the screen-reader flag on
        // purpose, before the random flips begin.
        //
        // cleanupTestCase asks that it saw both, and the storm was leaving
        // that to chance: isActive() is polled every 500 ms rather than
        // signalled, so a flip recorded straight away records the value from
        // before it. Over a hundred and fifty steps that usually catches
        // both -- a sweep of ten seeds found two where it caught only false,
        // and the check then stood on one observation.
        observeScreenReader(true);
        observeScreenReader(false);

        // Every step except the two that write. Clicking about on the first
        // screen reaches almost none of these; jumping puts the monkey on
        // each with the container's real wiring behind it.
        //
        // stepWriting and stepDone stay out. Their controls call startWrite(),
        // ejectDrive() and reboot(). The locks would make the first two
        // refuse, and reboot() is behind isEmbeddedMode() -- false on a
        // desktop build, true on the boards these tests run on. Relying on
        // the callee to decline is what the locks exist not to do.
        var reachableSteps = [
            wiz.stepLanguageSelection, wiz.stepDeviceSelection,
            wiz.stepOSSelection, wiz.stepStorageSelection,
            wiz.stepHostnameCustomization, wiz.stepLocaleCustomization,
            wiz.stepUserCustomization, wiz.stepWifiCustomization,
            wiz.stepRemoteAccess, wiz.stepSecureBootCustomization,
            wiz.stepPiConnectCustomization, wiz.stepIfAndFeatures
        ];
        verify(reachableSteps.indexOf(wiz.stepWriting) < 0,
               "the writing step is not somewhere the monkey can go");
        verify(reachableSteps.indexOf(wiz.stepDone) < 0,
               "nor is the step after it");

        var keys = [Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Escape, Qt.Key_Return,
                    Qt.Key_Space, Qt.Key_Down, Qt.Key_Up, Qt.Key_Left,
                    Qt.Key_Right, Qt.Key_Backspace, Qt.Key_A, Qt.Key_Z];

        stormFinished = false;
        for (var step = 0; step < 250; ++step) {
            var targets = [];
            collectTargets(wiz, targets, 0);
            if (targets.length > maxTargets)
                maxTargets = targets.length;
            verify(targets.length > 0, "something is on screen at step " + step);

            var action = rndInt(19);
            if (action < 5) {
                var item = targets[rndInt(targets.length)];
                if (!isDestructive(item)) {
                    // Aim somewhere inside it, not always the centre.
                    var x = rndInt(Math.max(1, Math.floor(item.width)));
                    var y = rndInt(Math.max(1, Math.floor(item.height)));
                    mouseClick(item, x, y);
                    ++clicks;
                }
            } else if (action < 8) {
                keyClick(keys[rndInt(keys.length)]);
            } else if (action < 10) {
                // Text where text may or may not be wanted.
                keyClick(Qt.Key_A + rndInt(26));
            } else if (action < 12) {
                // Resize mid-interaction. Layout bindings are evaluated
                // against whatever size they are given, and a step that
                // only ever sees 800x600 never exercises the arithmetic.
                wiz.width = 320 + rndInt(900);
                wiz.height = 240 + rndInt(700);
            } else if (action < 14) {
                // Focus somewhere nobody navigated to, which is how a
                // control that assumes it was reached by Tab gets found.
                var f = targets[rndInt(targets.length)];
                if (f.forceActiveFocus !== undefined && !isDestructive(f))
                    f.forceActiveFocus();
            } else if (action < 16) {
                var dest = reachableSteps[rndInt(reachableSteps.length)];
                wiz.jumpToStep(dest);
                stepsSeen[dest] = true;
                ++jumps;
            } else if (action < 17) {
                // On and off underneath whoever is standing on a step.
                //
                // Every step registers a different focus ring depending on
                // it -- help text is in the ring only when a screen reader
                // is present, and several steps register other items too --
                // and the rings are registered rather than walked, so a step
                // that built one on the wrong side of this flag keeps it.
                // Nothing else here crosses the transition; the accessibility
                // cases all stand on one side of it for their whole run.
                TestAccessibility.setActive(rndInt(2) === 0);
                ++accessibilityFlips;
                // What the wizard actually sees. isActive() is polled every
                // 500 ms rather than signalled, so recording the flag would
                // prove nothing about whether the rings were ever rebuilt.
                screenReaderSeen[String(ImageWriterSingleton.screenReaderActive)] = true;
            } else if (action < 18) {
                // Which customisation steps exist at all. In the application
                // these four are set once, from the capabilities of the
                // operating system just chosen, and never change again while
                // the user is inside them -- so the sixteen combinations are
                // never seen, and neither is a change of shape underneath
                // someone standing on a step that has just stopped existing.
                // getLastCustomizationStep() and the chaining around it read
                // all four, and the sidebar is built from them.
                wiz.secureBootAvailable = rndInt(2) === 0;
                wiz.piConnectAvailable = rndInt(2) === 0;
                wiz.ccRpiAvailable = rndInt(2) === 0;
                wiz.ifAndFeaturesAvailable = rndInt(2) === 0;
                ++shapeChanges;
                // Recorded, because the flags moving is not the same as
                // the wizard's shape moving with them: this is the value the
                // chaining and the sidebar are built from.
                if (typeof wiz.getLastCustomizationStep === "function")
                    lastStepsSeen[wiz.getLastCustomizationStep()] = true;
            } else {
                // Changing language calls QQmlEngine::retranslate(), which
                // re-evaluates every qsTr() binding in the tree at once --
                // mid-transition, with popups open, whatever state the storm
                // has left. Nothing else here disturbs that many bindings in
                // one go, and a binding that only resolves on first load
                // shows up when it is asked a second time.
                var langs = ImageWriterSingleton.getTranslations();
                if (langs && langs.length > 0) {
                    ImageWriterSingleton.changeLanguage(langs[rndInt(langs.length)]);
                    ++languagePicks;
                }
            }

            // Whatever holds focus now: if it is interactive, a screen reader
            // has to have something to announce for it. Checked here rather
            // than at the end because focus moves constantly and the item
            // holding it mid-storm is the one a user would be on.
            var focused = wiz.Window.activeFocusItem;
            if (focused && focused.activeFocusOnTab === true) {
                var an = focused.Accessible ? (focused.Accessible.name || "") : "";
                var ad = focused.Accessible ? (focused.Accessible.description || "") : "";
                var tx = (typeof focused.text === "string") ? focused.text : "";
                if (an.length === 0 && ad.length === 0 && tx.length === 0) {
                    var key = (focused.objectName || "") + "/" + focused.toString();
                    namelessFocused[key] = (namelessFocused[key] || 0) + 1;
                }
            }

            // Clicking Next enough times walks the wizard forward on its own,
            // so the monkey arrives at the writing step without ever being
            // sent there. Nothing can write -- there is no drive and no
            // destination -- but the screen whose job is writing is not one
            // to linger on, so it is shown the door immediately.
            if (wiz.currentStep === wiz.stepWriting
                    || wiz.currentStep === wiz.stepDone) {
                ++rescues;
                wiz.jumpToStep(wiz.stepOSSelection);
            }

            // The safety invariant comes first: nothing may have started a
            // write, and nothing may have put a device within reach.
            verify(!writeStarted, "no write began at step " + step
                                  + " (seed " + data.seed + ")");
            compare(TestDrives.count(), 0,
                    "the drive list stayed empty at step " + step);
            verify(!ImageWriterSingleton.readyToWrite(),
                   "the writer stayed unready at step " + step);

            // Then the liveness one: the wizard is still there and answering.
            verify(wiz !== null, "wizard survived step " + step);
            verify(wiz.width >= 0, "wizard still answers at step " + step);
        }

        // Geometry survived the resizing: a layout that broke under a size
        // it did not expect shows up as a negative or non-finite dimension
        // rather than as a crash.
        var all = [];
        collectTargets(wiz, all, 0);
        for (var k = 0; k < all.length; ++k) {
            var it = all[k];
            verify(it.width >= 0 && it.height >= 0,
                   "item " + k + " has sane size after seed " + data.seed);
            verify(it.width === it.width && it.height === it.height,
                   "item " + k + " size is a number after seed " + data.seed);
        }

        // The run has to have done something. Without this the case would
        // stay green if collectTargets() ever stopped finding the tree.
        console.log("ChaosMonkey seed", data.seed, "clicks", clicks,
                    "widest tree", maxTargets);
        stormFinished = true;

        verify(clicks > 20, "the monkey landed real clicks (" + clicks + ")");
        verify(maxTargets > 30, "the wizard tree was walked (" + maxTargets + ")");

        // Counted, because a jump that silently stopped working would leave
        // the run green while covering only the first screen again.
        var reached = Object.keys(stepsSeen).length;
        languagePicksTotal += languagePicks;
        shapeChangesTotal += shapeChanges;
        accessibilityFlipsTotal += accessibilityFlips;
        console.log("ChaosMonkey seed", data.seed, "jumps", jumps,
                    "distinct steps", reached, "language picks", languagePicks,
                    "shape changes", shapeChanges,
                    "accessibility flips", accessibilityFlips,
                    "last-step values", JSON.stringify(Object.keys(lastStepsSeen)));
        verify(jumps > 5, "the monkey moved between steps (" + jumps + ")");
        verify(reached > 3, "and saw several of them (" + reached + ")");

        // Not asserted to be zero: walking into the writing step is something
        // a user can do too, and it is the three locks above -- checked on
        // every one of the 250 steps -- that make it harmless. Recorded so a
        // run that started landing there constantly is visible.
        var nameless = Object.keys(namelessFocused);
        console.log("ChaosMonkey seed", data.seed, "left the writing step",
                    rescues, "times; focusable items with nothing to announce:",
                    nameless.length, JSON.stringify(nameless.slice(0, 4)));
        verify(!writeStarted, "and no write began on any of them");

        // And it is still usable rather than merely alive.
        verify(wiz.visible !== undefined, "wizard is still a live item");
        verify(!writeStarted, "no write began during seed " + data.seed);
    }
}
