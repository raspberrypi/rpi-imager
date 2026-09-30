/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Random interaction against the dialogs, which the wizard chaos run barely
 * reaches.
 *
 * Dialogs are where modality, focus traps and confirmation delays live, and
 * several of them guard something destructive -- the system-drive
 * confirmation above all. The property is that storming one cannot confirm
 * it by accident: a dialog that fires its accepted signal without the user
 * having satisfied whatever it asks for is the failure this looks for.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "ChaosDialogs"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Item {
        id: overlay
        anchors.fill: parent
    }

    property int rngState: 1
    property int confirmations: 0
    property int escapes: 0
    // Set when the storm's loop is left, and checked in cleanup() rather
    // than after the loop. A test function can be abandoned part-way -- a
    // step that spends long enough inside a nested event loop is enough --
    // and every check written after the loop goes with it, leaving a pass
    // that stands for nothing. cleanup() still runs.
    property bool stormFinished: false
    property int casesRun: 0
    property int dialogsOpened: 0
    property int safeDefaultChecks: 0


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

    // Whether an item is inside any popup at all.
    //
    // A dialog's contents hang off a QQuickPopupItem, so an ancestor of that
    // type means focus is inside a dialog -- this one, or one it opened. App
    // options opens two, and focus moving into either is the second dialog
    // taking the keyboard, which is what modality is. By type name because
    // QML has no other handle on it.
    function isInsideAnyPopup(item) {
        for (var p = item; p; p = p.parent) {
            if (String(p).indexOf("QQuickPopupItem") === 0)
                return true;
        }
        return false;
    }

    // Is `item` inside `root`'s subtree? Used to check that a modal dialog
    // keeps keyboard focus inside itself.
    function isDescendant(root, item) {
        var p = item;
        while (p) {
            if (p === root) return true;
            p = p.parent;
        }
        return false;
    }

    // Only real Items: a dialog is a Popup, and its children include objects
    // mouseClick() will not take. mapToItem is the cheapest thing every Item
    // has and nothing else does.
    function isItem(o) {
        return o !== null && o !== undefined && typeof o.mapToItem === "function";
    }

    function collectTargets(item, out, depth) {
        if (!isItem(item) || depth > 10 || out.length > 200) return;
        if (item.visible === false) return;
        if (item.width > 0 && item.height > 0) out.push(item);
        var kids = item.children;
        for (var i = 0; kids && i < kids.length; ++i)
            collectTargets(kids[i], out, depth + 1);
    }

    // The visual tree of a Popup hangs off contentItem, not the Popup.
    function visualRoot(dlg) {
        return isItem(dlg) ? dlg : (dlg && dlg.contentItem ? dlg.contentItem : null);
    }

    // Every debug switch DebugOptionsDialog can write, so the storm clicking
    // Apply does not leave one set for the rest of the run. These are global
    // on the one ImageWriter every QML file shares, and getDebugForceSecureBoot()
    // in particular feeds OSSelectionStep's secureBootAvailable -- leaving it
    // on makes tst_os_change_scrub fail, which is what it was doing.
    readonly property var debugSwitches: [
        "AsyncIO",
        "AsyncQueueDepth",
        "CustomFastbootGadget",
        "DirectIO",
        "ForceSecureBoot",
        "IgnoreDeviceLimits",
        "IPv4Only",
        "PeriodicSync",
        "Rpiboot",
        "SignFastbootGadget",
        "SkipEndOfDevice",
        "VerboseLogging"
    ]
    property var savedDebug: ({})

    // Restored per case rather than at the end of the file: the language is
    // process-wide, every case in this binary shares one ImageWriter, and a
    // dialog left translated would take the rest of the run with it.
    property string savedLanguage: ""
    // Totalled across the file and checked once at the end, rather than per
    // case: a case can end early with none of these, but a run where the
    // dimension stopped firing altogether would otherwise go on passing
    // while testing one fewer thing than it says it does.
    property int languagePicksTotal: 0


    function init() {
        // Same envelope as the wizard run: nothing to write to, and the
        // writer refuses even if a confirmation somehow fires.
        TestDrives.clear();
        ImageWriterSingleton.setDst("");
        verify(!ImageWriterSingleton.readyToWrite(),
               "no destination while the dialogs are stormed");
        confirmations = 0;
        escapes = 0;

        // Warnings become failures here too. A dialog whose binding falls
        // over keeps its old value and carries on looking fine, which is
        // exactly what a storm of random input produces.
        failOnWarning(/TypeError/);
        failOnWarning(/Binding loop/);
        failOnWarning(/is not a function/);
        failOnWarning(/ReferenceError/);
        failOnWarning(/Unable to assign/);

        savedLanguage = ImageWriterSingleton.getCurrentLanguage();

        savedDebug = ({});
        for (var i = 0; i < debugSwitches.length; ++i) {
            var g = "getDebug" + debugSwitches[i];
            if (typeof ImageWriterSingleton[g] === "function")
                savedDebug[debugSwitches[i]] = ImageWriterSingleton[g]();
        }
    }

    function cleanup() {
        verify(stormFinished, "the storm reached the end of its loop");
        // Put every one back, whatever the storm did to it.
        for (var i = 0; i < debugSwitches.length; ++i) {
            var k = debugSwitches[i];
            var st = "setDebug" + k;
            if (savedDebug[k] !== undefined
                    && typeof ImageWriterSingleton[st] === "function")
                ImageWriterSingleton[st](savedDebug[k]);
        }
        // English is the source language; there is no call that takes a
        // translator back off, so it stands in when the system locale
        // matched none and getCurrentLanguage() had nothing to report.
        ImageWriterSingleton.changeLanguage(
            savedLanguage.length > 0 ? savedLanguage : "English");
        TestDrives.clear();
        ImageWriterSingleton.setDst("");
    }

    function cleanupTestCase() {
        // The check this file exists for -- that focus cannot leave a modal
        // dialog -- is reached only when the dialog really opened, and
        // nothing required that. Seven cases in seven do open today, so a
        // dialog that stopped opening under test would take its own check
        // with it and leave the storm green.
        //
        // All of them, not most: a dialog that needs a precondition to open
        // should fail here and be handled deliberately, rather than skip its
        // check quietly.
        compare(dialogsOpened, casesRun,
                "every dialog opened, so every modality check ran");

        // The other property gated on a per-case flag, and the sharper of
        // the two: a dialog explaining a consequence must open with the safe
        // button under the keyboard, because the Return a user presses
        // without reading has to change nothing. Two cases in seven carry
        // understandingOnly, and nothing required that any did -- dropping
        // the flag from both would retire the check in silence.
        verify(safeDefaultChecks > 0,
               "some dialog was asked what the first Return does ("
               + safeDefaultChecks + ")");

        verify(languagePicksTotal > 0,
               "the storm changed language at least once");
    }

    // Built from the module, the way the application builds them, rather than
    // from a path relative to this file. The relative form quietly stopped
    // resolving when the file was run from anywhere else, and the skip below
    // then reported five green rows that had tested nothing.
    Component { id: confirmSystemDriveComponent; ConfirmSystemDriveDialog { } }
    Component { id: confirmOtpComponent;        ConfirmOtpProgramDialog   { } }
    Component { id: repositoryComponent;        RepositoryDialog          { } }
    Component { id: appOptionsComponent;        AppOptionsDialog          { } }
    Component { id: debugOptionsComponent;      DebugOptionsDialog        { } }
    // Two that guard something and were stormed by nothing. The first is
    // what stands between the user and system drives appearing in the
    // picker; the second gates a real privilege change. Neither asks for
    // anything to be typed, so a stray press is all a mistake would take.
    Component { id: confirmUnfilterComponent;   ConfirmUnfilterDialog     { } }
    Component { id: sudoWarningComponent;       PasswordlessSudoWarningDialog { } }

    function test_storm_data() {
        return [
            { tag: "confirm-system-drive", comp: confirmSystemDriveComponent, needsOverlay: true },
            { tag: "confirm-otp-program",  comp: confirmOtpComponent,        needsOverlay: true },
            { tag: "repository",           comp: repositoryComponent },
            { tag: "app-options",          comp: appOptionsComponent },
            { tag: "debug-options",        comp: debugOptionsComponent },
            // Understanding confirmations: two buttons, nothing asked of the
            // user but that they read it. ConfirmationPolicy says so in as
            // many words, so a storm reaching the confirming button is the
            // design working, not a defect -- what is asked of these is the
            // weaker property below.
            { tag: "confirm-unfilter",     comp: confirmUnfilterComponent,
              needsOverlay: true, understandingOnly: true },
            { tag: "sudo-warning",         comp: sudoWarningComponent,
              understandingOnly: true },
        ];
    }

    function test_storm(data) {
        // Reset per row: sharing the generator across rows made each
        // sequence depend on which rows had run before it, so a failure
        // could not be reproduced by running that row alone.
        // Reset per row, but not to a constant. Resetting is what keeps a
        // failing row reproducible on its own; hardcoding the value meant
        // every run explored the same single sequence per dialog, for ever.
        // RPI_CHAOS_SEED varies it, as it does for the other three storms.
        var override = TestEnv.value("RPI_CHAOS_SEED");
        rngState = override.length > 0 ? parseInt(override) : 20260912;

        // No skip path: a dialog that will not build is a failure, not a row
        // to pass over. Every one of these is a type the module exports.
        // overlayParent is a *required* property on some of these, so it has
        // to arrive with the object rather than be set afterwards -- and it
        // cannot go in the Component above, because the ones that do not
        // declare it refuse to compile with it.
        var dlg = data.comp.createObject(overlay,
                                         data.needsOverlay ? { overlayParent: overlay } : {});
        verify(dlg !== null, "dialog " + data.tag + " was created");

        // Watch for an acceptance nobody asked for.
        if (dlg.confirmed !== undefined)
            dlg.confirmed.connect(function() { testCase.confirmations++; });

        // Popup.open() runs an enter transition, and "opened" only becomes
        // true once it finishes. Modality and focus containment are not
        // enforced until then, so the property below is only meaningful
        // after this settles -- and asserting it earlier says nothing about
        // the dialog, only about the harness.
        if (dlg.open !== undefined)
            dlg.open();
        var reallyOpen = false;
        if (dlg.opened !== undefined) {
            for (var w = 0; w < 50 && !dlg.opened; ++w)
                wait(20);
            reallyOpen = dlg.opened === true;
        }
        console.log("ChaosDialogs", data.tag, "opened:", reallyOpen);
        ++testCase.casesRun;
        if (reallyOpen)
            ++testCase.dialogsOpened;

        // A dialog that explains a consequence and offers two buttons has to
        // open with the safe one under the keyboard: the press a user makes
        // without reading is the one this exists to catch, and it must be
        // the one that changes nothing.
        if (reallyOpen && data.understandingOnly) {
            keyClick(Qt.Key_Return);
            wait(1);
            compare(confirmations, 0,
                    data.tag + ": the first Return did not confirm it");
            ++testCase.safeDefaultChecks;
        }

        var keys = [Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space, Qt.Key_Escape,
                    Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Down, Qt.Key_Up];
        var clicks = 0;
        var languagePicks = 0;

        stormFinished = false;
        for (var step = 0; step < 150; ++step) {
            var targets = [];
            collectTargets(visualRoot(dlg), targets, 0);
            if (targets.length === 0)
                break;                     // dialog closed itself; that is allowed

            var action = rndInt(12);
            if (action < 4) {
                keyClick(keys[rndInt(keys.length)]);
            } else if (action < 11) {
                var it = targets[rndInt(targets.length)];
                var x = rndInt(Math.max(1, Math.floor(it.width)));
                var y = rndInt(Math.max(1, Math.floor(it.height)));
                mouseClick(it, x, y);
                ++clicks;
            } else {
                // A dialog is its own tree of qsTr() bindings, separate from
                // the wizard's, and retranslate() re-evaluates them all while
                // this one is open. The wizard storm cannot reach them.
                var langs = ImageWriterSingleton.getTranslations();
                if (langs && langs.length > 0) {
                    ImageWriterSingleton.changeLanguage(langs[rndInt(langs.length)]);
                    ++languagePicks;
                }
            }

            // A modal dialog must keep keyboard focus inside itself. Tab
            // and Backtab are in the key set above, so if the focus ring
            // leaks the storm will walk straight out of the dialog and into
            // whatever is behind it.
            if (reallyOpen && dlg.modal === true) {
                var focused = overlay.Window.activeFocusItem;
                // Two things this deliberately does not count. The window's
                // own root item is where focus rests when nothing holds it,
                // which is not the keyboard reaching past the dialog; and
                // anything inside a popup is inside a dialog. Between them
                // they were every one of the 215 escapes this reported
                // across five dialogs before the distinction was made, and
                // not one of them was a defect.
                if (focused !== null && focused !== undefined
                        && focused !== overlay
                        && focused.activeFocusOnTab === true
                        && !isInsideAnyPopup(focused)) {
                    if (!isDescendant(visualRoot(dlg), focused)
                            && !isDescendant(dlg, focused)) {
                        console.log("FOCUS-ESCAPE", data.tag, "step", step,
                                    "focused:", focused,
                                    "dlgVisible:", dlg.visible,
                                    "dlgOpened:", (dlg.opened !== undefined ? dlg.opened : "n/a"),
                                    "dlgModal:", dlg.modal);
                        escapes++;
                        // A failure, not a note. What is counted now is an
                        // operable control outside every dialog on screen,
                        // and nothing reaches it: this is here so that a
                        // change letting the keyboard past a modal dialog
                        // says so at once rather than adding to a tally.
                        fail("focus left the modal dialog at step " + step
                             + " (" + data.tag + "), focused: " + focused);
                    }
                }
            }

            // The safety envelope has to hold throughout.
            compare(TestDrives.count(), 0, "no drive appeared at step " + step);
            verify(!ImageWriterSingleton.readyToWrite(),
                   "writer stayed unready at step " + step);
        }

        stormFinished = true;

        // The property the file opens by naming, and it was counted and
        // logged and never asked about. Confirming one of these is not a
        // thing a hundred and fifty random presses may do: the system-drive
        // one wants its name typed and the repository one waits.
        //
        // The two understanding confirmations are excluded on purpose. They
        // exist to be read, not to be difficult, and a storm that tabs to
        // the confirming button and presses it has done what a user may do.
        // What they are asked instead is that the first press does not do
        // it -- see the check where each is opened.
        if (!data.understandingOnly)
            compare(confirmations, 0,
                    data.tag + ": nothing confirmed it but the storm");

        languagePicksTotal += languagePicks;
        console.log("ChaosDialogs", data.tag, "clicks", clicks,
                    "confirmations", confirmations, "focus escapes", escapes,
                    "language picks", languagePicks);
        dlg.destroy();
    }
}
