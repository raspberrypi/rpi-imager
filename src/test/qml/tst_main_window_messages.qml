/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * How the application says something has gone wrong.
 *
 * Every failure the writer reports -- a device that could not be opened, a
 * card that stopped responding, a hash that did not match -- arrives at
 * main.qml's onError and is put in front of the user by one dialog. It was
 * uncovered, which means nothing checked that a failure reaches the screen at
 * all. A write that fails silently is the worst outcome this application has:
 * the user takes the card away believing it worked.
 *
 * Two dialogs, deliberately different in kind. The error dialog can be
 * dismissed -- the failure has already happened and there is nothing further
 * to decide. The permission warning cannot: it is raised when the application
 * lacks the privileges it needs to write at all, so clicking past it would
 * leave the user in an application that cannot do the one thing they opened
 * it for. Its closePolicy refuses a click outside, and Escape quits the
 * application outright rather than dismissing it.
 *
 * That last one is not exercised here, for the same reason the force-quit
 * button in the quit confirmation is not: pressing it would end the test run
 * rather than assert anything. What is checked is that the dialog is raised,
 * carries the message, and cannot be dismissed by accident.
 *
 * main.qml is the application entry rather than an exported type, so it is
 * loaded by path -- from the copied module, so a coverage run instruments the
 * same file it drives.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "MainWindowMessages"
    when: windowShown

    property var win: null

    function init() {
        const component = Qt.createComponent(__qmlModuleRoot + "main.qml")
        compare(component.status, Component.Ready, component.errorString())
        // Shown, because dialogs parented to the window's overlay need it up.
        // The run is offscreen, so nothing appears.
        win = component.createObject(null, { visible: true })
        verify(win, "the application window was created")
        tryVerify(function () { return win.visible }, 3000, "the window is up")
    }

    function cleanup() {
        // Leave the writer where the other test files expect it.
        ImageWriterSingleton.onCancelled()
        if (win) {
            win.destroy()
            win = null
        }
    }

    // A Popup is not an Item child, so it does not turn up in a walk of
    // children; both dialogs carry an objectName for the same reason the
    // others in this window do.
    function errorDialog() {
        const d = findChild(win, "errorDialog")
        verify(d, "the error dialog was found")
        return d
    }

    function permissionDialog() {
        const d = findChild(win, "permissionWarningDialog")
        verify(d, "the permission warning dialog was found")
        return d
    }

    function collectText(item) {
        if (!item)
            return ""
        let out = item.text !== undefined ? String(item.text) + " " : ""
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++)
            out += collectText(kids[i])
        return out
    }

    function buttonNamed(item, label) {
        if (!item)
            return null
        if (item.text !== undefined && String(item.text) === label
                && item.clicked !== undefined)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = buttonNamed(kids[i], label)
            if (found)
                return found
        }
        return null
    }

    // -- A failure reaches the screen --------------------------------------

    function test_a_reported_failure_is_put_in_front_of_the_user() {
        // The whole point. A write that fails without saying so leaves the
        // user taking the card away believing it worked.
        win.onError("Error writing to device during formatting")

        const d = errorDialog()
        tryVerify(function () { return d.opened }, 3000,
                  "a reported failure raises a dialog")
        compare(String(d.message), "Error writing to device during formatting",
                "carrying the writer's own words")
        compare(String(d.titleText), "Error", "under a heading that says so")
    }

    function test_the_message_is_shown_and_not_only_stored() {
        // A dialog holding the message in a property it never renders would
        // pass the case above and tell the user nothing.
        win.onError("The card stopped responding")

        const d = errorDialog()
        tryVerify(function () { return d.opened }, 3000)
        const shown = collectText(d.contentItem)
        verify(shown.indexOf("The card stopped responding") >= 0,
               "the message is on the screen, not just in a property: "
               + shown)
    }

    function test_a_second_failure_replaces_the_first() {
        // Failures can arrive in a run. The dialog has to say what went
        // wrong now, not what went wrong first.
        win.onError("First failure")
        const d = errorDialog()
        tryVerify(function () { return d.opened }, 3000)

        win.onError("Second failure")

        compare(String(d.message), "Second failure",
                "the latest failure is the one shown")
    }

    // -- And can be dismissed, because it is only news ---------------------

    function test_the_failure_dialog_can_be_dismissed_with_escape() {
        // The failure has already happened; there is nothing left to decide,
        // so the user is allowed to put the message away.
        win.onError("Something went wrong")
        const d = errorDialog()
        tryVerify(function () { return d.opened }, 3000)

        keyClick(Qt.Key_Escape)

        tryVerify(function () { return !d.visible }, 3000,
                  "escape closes a message that only reports")
    }

    function test_the_failure_dialog_offers_a_way_on() {
        win.onError("Something went wrong")
        const d = errorDialog()
        tryVerify(function () { return d.opened }, 3000)

        const button = buttonNamed(d.contentItem, CommonStrings.continueText)
        verify(button !== null, "there is a button to carry on with")
        verify(button.enabled)
        verify(button.activeFocusOnTab,
               "and the keyboard can reach it, since a message that cannot be "
               + "dismissed without a mouse is a trap")
        verify(String(button.accessibleDescription).length > 0,
               "and it says what carrying on means")

        mouseClick(button)
        tryVerify(function () { return !d.visible }, 3000,
                  "and pressing it puts the message away")
    }

    // -- The one that cannot be dismissed ----------------------------------

    function test_a_permission_problem_is_raised_with_its_message() {
        // Raised when the application does not have the privileges to write.
        win.onPermissionWarning("Not running with administrator privileges")

        const d = permissionDialog()
        tryVerify(function () { return d.opened }, 3000,
                  "a permission problem is raised")
        compare(String(d.warningMessage),
                "Not running with administrator privileges")
        const shown = collectText(d.contentItem)
        verify(shown.indexOf("administrator privileges") >= 0,
               "and shown rather than only stored: " + shown)
    }

    function test_a_permission_problem_cannot_be_clicked_past() {
        // Unlike a failure report, this one is not news: the application
        // cannot do what the user opened it for until it is dealt with.
        // Dismissing it by clicking beside it would leave them in an
        // application that silently cannot write.
        win.onPermissionWarning("Not running with administrator privileges")

        const d = permissionDialog()
        tryVerify(function () { return d.opened }, 3000)

        compare(d.closePolicy, Popup.NoAutoClose,
                "no click outside and no stray escape dismisses it")

        // Escape is deliberately not pressed: this dialog's escape handler
        // quits the application, which would end the run rather than assert
        // anything. What matters here is that it is not dismissible by
        // accident, which the policy above is.
        verify(d.visible, "and it is still there")
    }
}
