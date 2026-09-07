/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The application window's one piece of behaviour: what closing it does
 * while a write is running.
 *
 * Clicking the window's close button in the middle of a write must not
 * take the write with it. The card is partly written and not synced, the
 * device is still open, and quitting there leaves a board that will not
 * boot and no message saying why. So the close is refused and a
 * confirmation is raised instead, and quitting for real needs a second,
 * deliberate act.
 *
 * main.qml is the application entry rather than an exported type, so the
 * test loads it by path. It was at 0%: nothing had ever instantiated it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "MainWindow"
    when: windowShown

    property var win: null

    function init() {
        // From the copied module, not the source tree: the coverage run
        // instruments that copy, and loading the original would drive the
        // file while reporting it as never run.
        var component = Qt.createComponent(__qmlModuleRoot + "main.qml")
        compare(component.status, Component.Ready, component.errorString())
        // Shown, because a window that was never shown does not emit
        // closing when close() is called, and that signal is the whole
        // subject here. The run is offscreen, so nothing appears.
        win = component.createObject(null, { visible: true })
        tryVerify(function () { return win.visible }, 3000, "the window is up")
        verify(win, "the application window was created")
    }

    function cleanup() {
        // Leave the writer where the other test cases expect to find it.
        ImageWriterSingleton.onCancelled()
        if (win) {
            win.destroy()
            win = null
        }
    }

    function quitDialog() {
        var d = findChild(win, "quitWhileWritingDialog")
        verify(d, "found the quit confirmation")
        return d
    }

    function test_closing_when_idle_just_closes() {
        win.close()

        wait(200)
        verify(!quitDialog().opened, "nothing was asked")
    }

    function test_closing_during_a_write_is_refused() {
        // onFinalizing lands in a state the wizard counts as writing.
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () {
            return ImageWriterSingleton.writeState === ImageWriterSingleton.Finalizing
        }, 3000, "a write is in progress")

        win.close()

        tryVerify(function () { return quitDialog().opened }, 3000,
                  "the confirmation was raised instead of quitting")
        verify(win.visible, "and the window is still up rather than gone")
    }

    function test_the_confirmation_is_not_raised_once_forced() {
        // The dialog's own Quit button sets forceQuit and closes again;
        // without the flag that would raise the same dialog for a second
        // time and there would be no way out of it.
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () {
            return ImageWriterSingleton.writeState === ImageWriterSingleton.Finalizing
        }, 3000)

        win.forceQuit = true
        win.close()

        wait(200)
        verify(!quitDialog().opened, "the second close went through")
    }

    // ── The card being pulled out ─────────────────────────────────────
    //
    // When the selected drive disappears, the destination the writer holds
    // has to go with it. Leaving it set means a later write aims at a drive
    // that is not there -- or, worse, at whatever the operating system gave
    // that name to next.
    //
    // Whether to say anything depends on where the user is. Past the card
    // chooser they are moved back to it and told why; on the chooser itself
    // they watched the row vanish, and a dialog explaining what they just
    // saw is noise.

    function container() {
        var c = findChild(win, "mainWizardContainer")
        verify(c, "found the wizard container")
        return c
    }

    function removalDialog() {
        var d = findChild(win, "storageRemovedDialog")
        verify(d, "found the storage-removed dialog")
        return d
    }

    function test_losing_the_drive_past_the_chooser_goes_back_and_says_so() {
        container().currentStep = container().stepWriting
        container().selectedStorageName = "Generic Mass-Storage"
        ImageWriterSingleton.setSrc("file:///tmp/whatever.img")
        ImageWriterSingleton.setDst("/dev/null", 1024 * 1024)
        verify(ImageWriterSingleton.readyToWrite())

        win.onSelectedDeviceRemoved()

        compare(container().selectedStorageName, "")
        verify(!ImageWriterSingleton.readyToWrite(),
               "the writer no longer has a target")
        compare(container().currentStep, container().stepStorageSelection,
                "and the user is back on the chooser")
        tryVerify(function () { return removalDialog().opened }, 3000,
                  "having been told why")

        removalDialog().close()
        tryVerify(function () { return !removalDialog().visible }, 3000)
    }

    function test_losing_the_drive_on_the_chooser_says_nothing() {
        // Nothing was navigated, so there is nothing to explain -- the row
        // disappearing is the explanation.
        container().currentStep = container().stepStorageSelection
        container().selectedStorageName = "Generic Mass-Storage"
        ImageWriterSingleton.setSrc("file:///tmp/whatever.img")
        ImageWriterSingleton.setDst("/dev/null", 1024 * 1024)

        win.onSelectedDeviceRemoved()

        compare(container().selectedStorageName, "",
                "the selection still goes")
        verify(!ImageWriterSingleton.readyToWrite())
        wait(300)
        verify(!removalDialog().opened, "but no dialog was raised")
    }

    function test_a_write_cancelled_by_removal_always_explains_itself() {
        // Distinct from the case above: a write was in progress, so the user
        // is told regardless of where they were standing.
        container().currentStep = container().stepStorageSelection
        container().selectedStorageName = "Generic Mass-Storage"
        ImageWriterSingleton.setSrc("file:///tmp/whatever.img")
        ImageWriterSingleton.setDst("/dev/null", 1024 * 1024)

        win.onWriteCancelledDueToDeviceRemoval()

        compare(container().selectedStorageName, "")
        verify(!ImageWriterSingleton.readyToWrite())
        compare(container().currentStep, container().stepStorageSelection)
        tryVerify(function () { return removalDialog().opened }, 3000,
                  "the cancellation was explained")

        removalDialog().close()
        tryVerify(function () { return !removalDialog().visible }, 3000)
    }
}
