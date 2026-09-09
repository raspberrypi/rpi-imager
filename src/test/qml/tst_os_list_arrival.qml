/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * What the wizard does when the OS list turns up late.
 *
 * Started with no network, the wizard skips device selection -- there is no
 * list of boards to choose from -- and opens on OS selection instead. If the
 * list arrives afterwards, the container moves the user to device selection,
 * because a board is something they were never given the chance to pick.
 *
 * That navigation is guarded three ways, and two of the guards protect the
 * user from being moved somewhere they did not ask to go. The one that matters
 * most is the write: a write in progress must not be interrupted because the
 * network came back, or the user is pulled off the progress screen part-way
 * through an erase with no idea what happened to it. The other is the step
 * they are on: a user half-way through customisation must not be thrown back
 * to the start, losing their place, because a fetch finished in the
 * background.
 *
 * The first guard is hasNetworkConnectivity, and it is worth being careful
 * about here. It is derived from the singleton's OS list, which is shared by
 * every test file in the process, so it is not this file's to set. Any case
 * asserting "no navigation happened" would pass for the wrong reason if the
 * list were absent, because then the first guard alone stops it -- so each of
 * those asserts the precondition before relying on it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "OsListArrival"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Component {
        id: containerComponent
        WizardContainer {}
    }

    property var wiz: null

    function initTestCase() {
        wiz = containerComponent.createObject(testCase)
        verify(wiz, "the wizard container was created")
        wiz.overlayRootRef = testCase
    }

    function cleanupTestCase() {
        if (wiz) {
            wiz.destroy()
            wiz = null
        }
    }

    function init() {
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
    }

    function cleanup() {
        // Leave no write running for the next case, or for the next file.
        ImageWriterSingleton.onCancelled()
        tryVerify(function () { return !wiz.isWriting }, 3000)
    }

    // The list lives on the singleton, shared process-wide, so a case that
    // needs one says so rather than quietly testing nothing.
    function requireList() {
        if (!wiz.hasNetworkConnectivity) {
            skip("no OS list was fetched in this run, so the first of the "
                 + "three guards stops the navigation on its own and these "
                 + "cases would pass without exercising the other two; the "
                 + "suite as a whole covers this")
            return false
        }
        return true
    }

    function startWriting() {
        // onFinalizing lands in a state the container counts as a write in
        // progress.
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return wiz.isWriting }, 3000,
                  "the container sees a write running")
    }

    // -- The navigation it exists to do ------------------------------------

    function test_a_list_arriving_offers_the_board_choice_that_was_skipped() {
        if (!requireList())
            return
        wiz.jumpToStep(wiz.stepOSSelection)
        compare(wiz.currentStep, wiz.stepOSSelection,
                "where an offline start leaves the user")
        verify(!wiz.isWriting)

        ImageWriterSingleton.osListUnavailableChanged()

        compare(wiz.currentStep, wiz.stepDeviceSelection,
                "with a list in hand, the user is offered the board choice "
                + "they were never shown")
    }

    // -- The guards ---------------------------------------------------------

    function test_a_list_arriving_does_not_interrupt_a_write() {
        // The serious one. Being moved off the progress screen part-way
        // through an erase, because a background fetch finished, leaves the
        // user with no idea whether their card is still being written.
        if (!requireList())
            return
        wiz.jumpToStep(wiz.stepOSSelection)
        startWriting()

        // Both of the other conditions hold, so this case really does turn on
        // the write guard rather than on one of them.
        verify(wiz.hasNetworkConnectivity, "the network is up")
        compare(wiz.currentStep, wiz.stepOSSelection, "and the step matches")

        ImageWriterSingleton.osListUnavailableChanged()

        compare(wiz.currentStep, wiz.stepOSSelection,
                "a write in progress is not interrupted by the network "
                + "coming back")
    }

    function test_a_list_arriving_does_not_move_a_user_who_has_moved_on_data() {
        return [
            { tag: "storage",       step: wiz.stepStorageSelection },
            { tag: "hostname",      step: wiz.stepHostnameCustomization },
            { tag: "user",          step: wiz.stepUserCustomization },
            { tag: "wifi",          step: wiz.stepWifiCustomization },
            { tag: "writing",       step: wiz.stepWriting },
            { tag: "done",          step: wiz.stepDone },
        ]
    }

    function test_a_list_arriving_does_not_move_a_user_who_has_moved_on(data) {
        // Past OS selection the user has made choices. Throwing them back to
        // device selection because a fetch finished would lose their place,
        // and on the writing step it would take them off a running write.
        if (!requireList())
            return
        wiz.jumpToStep(data.step)
        compare(wiz.currentStep, data.step)
        verify(wiz.hasNetworkConnectivity,
               "the network guard is not what is being relied on here")

        ImageWriterSingleton.osListUnavailableChanged()

        compare(wiz.currentStep, data.step,
                "a user on the " + data.tag
                + " step stays where they put themselves")
    }

    function test_a_list_arriving_twice_does_not_move_the_user_again() {
        // The signal fires in both directions and more than once. Having
        // taken the user to device selection, a second one must not do
        // anything: they may have gone forward again by then.
        if (!requireList())
            return
        wiz.jumpToStep(wiz.stepOSSelection)

        ImageWriterSingleton.osListUnavailableChanged()
        compare(wiz.currentStep, wiz.stepDeviceSelection)

        wiz.jumpToStep(wiz.stepStorageSelection)
        ImageWriterSingleton.osListUnavailableChanged()

        compare(wiz.currentStep, wiz.stepStorageSelection,
                "the second one leaves them alone")
    }

    // -- And the sidebar it renames ----------------------------------------

    function test_the_sidebar_names_the_board_step_only_when_it_exists() {
        // Offline the device step is skipped, so it is left out of the
        // sidebar as well -- a step listed but unreachable reads as a stage
        // the user has failed to complete.
        const names = wiz.stepNames
        verify(names.length > 0, "the sidebar has names")
        if (wiz.hasNetworkConnectivity)
            compare(String(names[0]), "Device",
                    "with a list, the board step is listed first")
        else
            compare(String(names[0]), "OS",
                    "without one, the sidebar starts at the OS step")
    }
}
