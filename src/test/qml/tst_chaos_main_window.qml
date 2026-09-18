/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Random interaction against the whole application window.
 *
 * The other runs build a wizard container, or a single step, on their own.
 * Neither is what a user meets: the real window owns the overlay the popups
 * live in, the menu, the close handling, and a wizard wired to a running
 * ImageWriter. A dialog parented into a live overlay, a step torn down while
 * a popup above it is open -- reachable here and nowhere else.
 *
 * Same three locks: nothing may put a device within reach of a write.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosMainWindow"
    when: windowShown

    property var win: null
    property int rngState: 1
    property int controlsExamined: 0
    // Set when the storm's loop is left, and checked in cleanup() rather
    // than after the loop. A test function can be abandoned part-way -- a
    // step that spends long enough inside a nested event loop is enough --
    // and every check written after the loop goes with it, leaving a pass
    // that stands for nothing. cleanup() still runs.
    property bool stormFinished: false

    property bool writeStarted: false

    Connections {
        target: ImageWriterSingleton
        function onPreparationStatusUpdate(msg) { testCase.writeStarted = true }
        function onWriteProgress(now, total)    { testCase.writeStarted = true }
        function onSuccess()                    { testCase.writeStarted = true }
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

    function collect(item, out, depth) {
        if (!item || depth > 14 || out.length > 400)
            return;
        if (item.visible === false)
            return;
        if (item.width > 0 && item.height > 0)
            out.push(item);
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            collect(kids[i], out, depth + 1);
    }

    // Never a valid target, whatever the drive list says.
    // Controls that can be tabbed to but have no size left.
    //
    // Visible and focusable, so a screen reader will announce it and the
    // keyboard will stop on it, while there is nothing on screen to stop on.
    // Anything inside a Flickable is left alone: it may simply be scrolled
    // past the edge, which is not the same fault.
    function countCollapsedControls(item) {
        var n = 0;
        if (!item || item.visible === false)
            return 0;
        if (item.activeFocusOnTab === true && !insideAFlickable(item)) {
            ++controlsExamined;
            if (item.width === 0 || item.height === 0) {
                console.log("COLLAPSED", item, item.objectName);
                ++n;
            }
        }
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            n += countCollapsedControls(kids[i]);
        return n;
    }

    function insideAFlickable(item) {
        for (var p = item; p; p = p.parent) {
            if (p.contentY !== undefined && p.contentHeight !== undefined)
                return true;
        }
        return false;
    }

    function isDestructive(item) {
        var t = "";
        if (item.text !== undefined && typeof item.text === "string")
            t = item.text.toLowerCase();
        var n = (item.objectName || "").toLowerCase();
        return t.indexOf("write") >= 0 || t.indexOf("erase") >= 0
            || t.indexOf("format") >= 0 || n.indexOf("write") >= 0
            || n.indexOf("erase") >= 0 || n.indexOf("format") >= 0;
    }

    // How many popups are open right now. The reason this run exists is the
    // real overlay and the dialogs that live in it; if the storm never opens
    // one, it is a slower copy of the container run and proves nothing the
    // others do not. Counted rather than assumed.
    function countOpenPopups(item, depth) {
        if (!item || depth > 14)
            return 0;
        var n = 0;
        if (item.opened === true && item.contentItem !== undefined)
            ++n;
        // `data` for the same reason collectDialogs() uses it: a Popup is not
        // in the visual tree, so counting through children reports none open
        // however many are.
        var kids = item.data !== undefined ? item.data : item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            n += countOpenPopups(kids[i], depth + 1);
        return n;
    }

    // Dialogs the window owns that are safe to raise. Found by shape -- an
    // objectName ending in "Dialog" with an open() -- so one added later is
    // picked up without this file knowing about it.
    //
    // Three kinds are left alone. Quitting, which would end the run.
    // permissionWarningDialog, whose buttons reach execElevated(): with
    // passwordless sudo that succeeds, and an elevated Imager writes the
    // user's configuration as root. And saving or exporting, which writes a
    // file -- no run has managed it, but that is the dialog declining, not
    // the harness being unable.
    function collectDialogs(item, out, depth) {
        if (!item || depth > 14 || out.length > 40)
            return;
        var n = item.objectName || "";
        const lower = n.toLowerCase();
        if (n.length > 6 && n.slice(-6) === "Dialog"
                && lower.indexOf("quit") < 0
                && lower.indexOf("save") < 0
                && lower.indexOf("export") < 0
                && lower.indexOf("permissionwarning") < 0
                && typeof item.open === "function")
            out.push(item);
        // `data`, not `children`: a Popup is not an Item and never appears in
        // the visual tree, so walking children alone finds none of them --
        // which reads as "this window has no dialogs" and is why the first
        // version of this counted zero.
        var kids = item.data !== undefined ? item.data : item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            collectDialogs(kids[i], out, depth + 1);
    }

    // The wizard container, found by what it carries rather than by name:
    // the window's internals are not this test's to know.
    function findWizard(item, depth) {
        if (!item || depth > 10)
            return null;
        if (item.stepWriting !== undefined && item.currentStep !== undefined)
            return item;
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i) {
            var found = findWizard(kids[i], depth + 1);
            if (found)
                return found;
        }
        return null;
    }

    function init() {
        // 1. Nothing to write to, and the poller stopped.
        TestDrives.clear();
        compare(TestDrives.count(), 0, "the window has no drive to write to");

        // 2. No destination, so startWrite() refuses.
        ImageWriterSingleton.setDst("");
        verify(!ImageWriterSingleton.readyToWrite(), "the writer is not ready");

        // 3. And if one began anyway, this records it.
        writeStarted = false;

        failOnWarning(/TypeError/);
        failOnWarning(/Binding loop/);
        failOnWarning(/is not a function/);
        failOnWarning(/ReferenceError/);
        failOnWarning(/Unable to assign/);

        if (!win) {
            // From the copied module, not the source tree -- the coverage run
            // instruments the copy, as tst_main_window explains.
            var component = Qt.createComponent(__qmlModuleRoot + "main.qml");
            compare(component.status, Component.Ready, component.errorString());
            win = component.createObject(null, { visible: true });
            tryVerify(function () { return win.visible }, 5000, "the window is up");
        }
        verify(win, "the application window was created");
    }

    function cleanup() {
        verify(stormFinished, "the storm reached the end of its loop");
    }

    function cleanupTestCase() {
        // Leave the writer as the other files expect to find it: they share
        // one ImageWriter across the whole run.
        ImageWriterSingleton.onCancelled();
        ImageWriterSingleton.setDst("");
        if (win) {
            win.destroy();
            win = null;
        }
        TestDrives.clear();
    }

    function test_storm_data() {
        var override = TestEnv.value("RPI_CHAOS_SEED");
        if (override.length > 0)
            return [ { tag: "seed-" + override, seed: parseInt(override) } ];
        return [
            { tag: "seed-17",     seed: 17 },
            { tag: "seed-5003",   seed: 5003 },
            { tag: "seed-982451", seed: 982451 }
        ];
    }

    function test_storm(data) {
        rngState = data.seed;

        var clicks = 0;
        var maxTargets = 0;
        var rescues = 0;
        var popupsSeen = 0;
        var opened = 0;
        var atMinimum = 0;
        var collapsed = 0;
        controlsExamined = 0;
        var resizes = 0;

        var keys = [Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Escape, Qt.Key_Return,
                    Qt.Key_Space, Qt.Key_Down, Qt.Key_Up, Qt.Key_Left,
                    Qt.Key_Right, Qt.Key_Backspace];

        stormFinished = false;
        for (var step = 0; step < 150; ++step) {
            var targets = [];
            collect(win.contentItem, targets, 0);
            if (targets.length > maxTargets)
                maxTargets = targets.length;
            verify(targets.length > 0, "something is on screen at step " + step);

            var action = rndInt(13);
            if (action < 5) {
                var item = targets[rndInt(targets.length)];
                if (!isDestructive(item)) {
                    var x = rndInt(Math.max(1, Math.floor(item.width)));
                    var y = rndInt(Math.max(1, Math.floor(item.height)));
                    mouseClick(item, x, y);
                    ++clicks;
                }
            } else if (action < 8) {
                keyClick(keys[rndInt(keys.length)]);
            } else if (action < 10) {
                keyClick(Qt.Key_A + rndInt(26));
            } else if (action < 11) {
                // From the window's own minimum rather than a number picked
                // here: 640 was below it, so those cases asked for a size the
                // application can never be at, and the tightest layout it
                // genuinely has -- exactly the minimum -- was never tried.
                var mw = win.minimumWidth > 0 ? win.minimumWidth : 680;
                var mh = win.minimumHeight > 0 ? win.minimumHeight : 420;
                // Counted, not drawn. A nested rndInt(3) inside this branch
                // looked like one resize in three; consecutive draws from the
                // generator above are correlated, and four seeds out of ten
                // never took the minimum at all, failing the check below for
                // a reason that had nothing to do with the window.
                if ((resizes++ % 3) === 0) {
                    win.width = mw;
                    win.height = mh;
                    ++atMinimum;
                    // The tightest layout the application has. A control
                    // that collapses to nothing here cannot be clicked and
                    // cannot be seen, and there is no scrolling excuse for
                    // it: this is the size the window refuses to go below.
                    // The Pi's own 7-inch display is close to it.
                    waitForRendering(testCase);
                    collapsed += countCollapsedControls(win.contentItem);
                } else {
                    win.width = mw + rndInt(700);
                    win.height = mh + rndInt(500);
                }
            } else if (action < 12) {
                var f = targets[rndInt(targets.length)];
                if (f.forceActiveFocus !== undefined && !isDestructive(f))
                    f.forceActiveFocus();
            } else {
                // Raise one, so the clicks and keys above land on a dialog
                // sitting in the overlay the application really uses.
                var dlgs = [];
                collectDialogs(win, dlgs, 0);
                if (dlgs.length > 0) {
                    dlgs[rndInt(dlgs.length)].open();
                    ++opened;
                }
            }

            // Both trees: a Popup with no explicit parent goes to the
            // window's overlay, which is a sibling of contentItem and not
            // reachable by walking down from it. main.qml also keeps an
            // overlayRootItem inside the content, which is where the wizard
            // puts its own. Counting only one of them reads as "no dialog
            // ever opened" whether or not that is true.
            var open = countOpenPopups(win, 0);
            if (open > popupsSeen)
                popupsSeen = open;

            // Walking forward with Next reaches the writing step here as well.
            // Nothing can write, but it is not a screen to sit on.
            var wiz = findWizard(win.contentItem, 0);
            if (wiz && (wiz.currentStep === wiz.stepWriting
                        || wiz.currentStep === wiz.stepDone)) {
                ++rescues;
                wiz.jumpToStep(wiz.stepOSSelection);
            }

            verify(!writeStarted, "no write began at step " + step
                                  + " (seed " + data.seed + ")");
            compare(TestDrives.count(), 0,
                    "the drive list stayed empty at step " + step);
            verify(!ImageWriterSingleton.readyToWrite(),
                   "the writer stayed unready at step " + step);
            verify(win.visible, "the window is still up at step " + step);
        }

        stormFinished = true;

        console.log("ChaosMainWindow seed", data.seed, "clicks", clicks,
                    "widest tree", maxTargets, "left the writing step", rescues,
                    "most popups open at once", popupsSeen,
                    "dialogs raised", opened,
                    "times at the minimum size", atMinimum);

        // The run has to have done something. Without these the case stays
        // green if the window ever stops producing a tree to walk.
        verify(clicks > 10, "the monkey landed real clicks (" + clicks + ")");
        verify(maxTargets > 20, "the window tree was walked (" + maxTargets + ")");

        // The reason this run exists. Both of these read zero until the walk
        // was changed to follow `data` rather than `children`, and the run
        // passed the whole time -- a slower copy of the container one, with a
        // comment at the top claiming otherwise.
        verify(opened > 0, "dialogs were raised in the real window ("
                           + opened + ")");

        // popupsSeen is logged, not asserted. Popup.open() runs an enter
        // transition and `opened` only turns true once it finishes, so
        // whether a sample catches one depends on what the next action is --
        // Escape is in the key list and closes them. Seed 3 raised twelve
        // dialogs and this never saw one, which says nothing about the
        // window and everything about when the sample was taken. The count
        // that means something is the one above.
        verify(atMinimum > 0, "and the window was taken to its minimum size ("
                              + atMinimum + ")");
        compare(collapsed, 0,
                "no control collapsed to nothing at the minimum size");
        // Counted, because the check above is worth nothing if it looked at
        // nothing. Most of the window's focusable controls sit inside a
        // Flickable and are skipped -- being scrolled past an edge is not
        // the same fault -- so this is a handful, not a crowd.
        verify(controlsExamined > 0,
               "and something was looked at (" + controlsExamined + ")");

        // Nothing may have been left with a size it cannot draw at. A layout
        // that gave up under the smallest window the application allows shows
        // as a negative or non-finite dimension rather than as a crash.
        var all = [];
        collect(win.contentItem, all, 0);
        for (var k = 0; k < all.length; ++k) {
            verify(all[k].width >= 0 && all[k].height >= 0,
                   "item " + k + " has a sane size after seed " + data.seed);
            verify(all[k].width === all[k].width
                   && all[k].height === all[k].height,
                   "item " + k + " size is a number after seed " + data.seed);
        }
        verify(!writeStarted, "and no write began during seed " + data.seed);
    }
}
