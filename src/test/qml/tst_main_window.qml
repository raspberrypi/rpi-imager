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
}
