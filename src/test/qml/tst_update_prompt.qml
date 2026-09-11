/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The prompt that says a newer Imager is out.
 *
 * Three handlers, all uncovered: the two buttons and the escape key. It is
 * a small dialog, but it is shown unprompted over whatever the user was
 * doing, so what it does with each answer decides whether it is an
 * interruption or an obstruction.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "UpdatePrompt"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Component {
        id: dialogComponent

        UpdateAvailableDialog {
            id: dlg
            parent: testCase
            anchors.centerIn: parent
            version: "2.0.0"
            url: "https://www.raspberrypi.com/software/"

            property int pagesOpened: 0
            property string openedUrl: ""
            function openDownloadPage() {
                dlg.pagesOpened++
                dlg.openedUrl = String(dlg.url)
            }
        }
    }

    property var dialog: null

    SignalSpy { id: accepted; signalName: "accepted" }
    SignalSpy { id: rejected; signalName: "rejected" }

    function init() {
        dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog, "the dialog was created")
        accepted.target = dialog
        rejected.target = dialog
        accepted.clear()
        rejected.clear()
        dialog.open()
        tryVerify(function () { return dialog.opened }, 3000, "the prompt opened")
    }

    function child(name) {
        var c = findChild(dialog, name)
        verify(c, "found " + name)
        return c
    }

    // -- Saying no -------------------------------------------------------

    function test_no_closes_it_and_opens_nothing() {
        child("updateNoButton").clicked()

        compare(rejected.count, 1, "the offer was declined")
        compare(accepted.count, 0)
        compare(dialog.pagesOpened, 0, "and no browser was sent for")
        tryVerify(function () { return !dialog.visible }, 3000,
                  "and it got out of the way")
    }

    function test_escape_is_the_same_as_no() {
        // Whatever the user was doing, they can get back to it.
        dialog.escapePressed()

        compare(rejected.count, 1)
        compare(accepted.count, 0)
        compare(dialog.pagesOpened, 0)
        tryVerify(function () { return !dialog.visible }, 3000)
    }

    // -- Saying yes ------------------------------------------------------

    function test_update_opens_the_download_page() {
        child("updateYesButton").clicked()

        compare(dialog.pagesOpened, 1, "the download page was opened")
        compare(dialog.openedUrl, "https://www.raspberrypi.com/software/",
                "and it is the address the prompt was given")
        compare(accepted.count, 1)
        compare(rejected.count, 0)
    }

    function test_update_closes_the_prompt_behind_it() {
        // It has done its job; leaving it up would put it over the browser
        // the user has just been sent to.
        child("updateYesButton").clicked()

        tryVerify(function () { return !dialog.visible }, 3000)
    }

    function test_update_with_nowhere_to_go_still_closes() {
        // The prompt is raised from a version check that may not have
        // carried a link. Opening nothing is right; leaving the user stuck
        // in front of a dialog whose only button does nothing is not.
        dialog.url = ""

        child("updateYesButton").clicked()

        compare(accepted.count, 1)
        tryVerify(function () { return !dialog.visible }, 3000)
    }

    // -- Both answers are reachable and described ------------------------

    function test_both_answers_are_reachable_by_keyboard() {
        verify(child("updateNoButton").activeFocusOnTab)
        verify(child("updateYesButton").activeFocusOnTab)
        verify(String(child("updateNoButton").accessibleDescription).length > 0,
               "and each says what it will do")
        verify(String(child("updateYesButton").accessibleDescription).length > 0)
    }
}
