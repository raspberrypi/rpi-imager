/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * WritingStep: the last thing between the user and an erased device.
 *
 * The confirmation here is the one that names the drive and says all data
 * on it will be destroyed, and it is gated by a countdown so it cannot be
 * dismissed by someone already leaning on Enter. Two routes bypass parts of
 * that, and both were untested: "Disable warnings" skips the dialog
 * outright, and an attached screen reader keeps the dialog but drops the
 * countdown, because a blind user needs time to hear the content rather
 * than watch a timer they cannot see.
 *
 * The step was at 0% -- nothing had ever instantiated it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "WritingStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property bool disableWarnings: false
        property bool customizationSupported: true
        property bool featUsbGadgetEnabled: false
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property bool piConnectEnabled: false
        property bool ifSpiEnabled: false
        property string ifSerial: ""
        property string selectedDeviceName: "Raspberry Pi 5"
        property string selectedOsName: "Raspberry Pi OS (64-bit)"
        property string selectedStorageName: "Generic Mass-Storage 32 GB"
        property int steps: 0
        function nextStep() { steps++ }
    }

    Component {
        id: stepComponent
        WritingStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function init() {
        TestAccessibility.setActive(false)
        fakeContainer.disableWarnings = false
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
    }

    function cleanup() {
        TestAccessibility.setActive(false)
        if (step) {
            step.destroy()
            step = null
        }
    }

    function confirmDialog() {
        var d = findChild(step, "confirmWriteDialog")
        verify(d, "found the confirmation dialog")
        return d
    }

    // The dialog reads ImageWriterSingleton.screenReaderActive, which is a
    // cached value refreshed by a 500 ms poll -- QAccessible has no change
    // notification on macOS or Windows, so it is sampled rather than
    // observed. Toggling it and opening the dialog in the same frame reads
    // the previous value, which is a race in the test and not in the app: a
    // real user turning a screen reader on is not opening a dialog in the
    // same 500 ms.
    function useScreenReader() {
        TestAccessibility.setActive(true)
        tryVerify(function () { return ImageWriterSingleton.screenReaderActive },
                  4000, "the app noticed the screen reader")
    }

    function acceptButton() {
        var b = findChild(step, "confirmWriteAcceptButton")
        verify(b, "found the accept button")
        return b
    }

    // ── Getting to the confirmation ───────────────────────────────────

    function test_asking_to_write_raises_the_confirmation() {
        step.nextClicked()
        tryVerify(function () { return confirmDialog().opened }, 3000,
                  "the erase confirmation was raised")
    }

    function test_disabling_warnings_skips_the_confirmation() {
        // The documented purpose of the setting. Worth pinning because it is
        // the one place a user can remove this dialog, and it must remove
        // exactly this and not, say, fail to start the write.
        fakeContainer.disableWarnings = true

        step.nextClicked()

        wait(200)
        verify(!confirmDialog().opened, "no confirmation was raised")
    }

    // ── The countdown ─────────────────────────────────────────────────

    function test_the_confirmation_cannot_be_accepted_immediately() {
        // Without this a user holding Enter from the previous screen erases
        // the device without ever reading the dialog.
        step.nextClicked()
        tryVerify(function () { return confirmDialog().opened }, 3000)

        verify(!acceptButton().enabled, "accept is refused while the countdown runs")
        compare(confirmDialog().countdown, 2)
    }

    function test_the_countdown_ends_and_lets_the_write_through() {
        step.nextClicked()
        tryVerify(function () { return confirmDialog().opened }, 3000)

        tryVerify(function () { return acceptButton().enabled }, 6000,
                  "accept became available once the countdown finished")
        verify(confirmDialog().allowAccept)
    }

    function test_closing_the_confirmation_puts_the_countdown_back() {
        // Reopening has to start the wait again rather than inherit the
        // elapsed one from a dialog that was dismissed.
        step.nextClicked()
        tryVerify(function () { return confirmDialog().opened }, 3000)
        tryVerify(function () { return acceptButton().enabled }, 6000)

        confirmDialog().close()

        tryVerify(function () { return !confirmDialog().allowAccept }, 3000)
        compare(confirmDialog().countdown, 2)
    }

    // ── With a screen reader attached ─────────────────────────────────

    function test_a_screen_reader_still_gets_the_confirmation() {
        // The dialog names the drive being erased. Unlike the understanding
        // confirmations elsewhere, this one is not skipped for assistive
        // technology -- it is the warning, not an interruption before one.
        useScreenReader()

        step.nextClicked()

        tryVerify(function () { return confirmDialog().opened }, 3000,
                  "the confirmation was still raised")
    }

    function test_a_screen_reader_does_not_wait_for_a_countdown_it_cannot_see() {
        // The countdown is a visual device. Someone listening to the dialog
        // is already spending longer on it than the timer would impose, so
        // making them wait as well is delay without protection.
        useScreenReader()

        step.nextClicked()
        tryVerify(function () { return confirmDialog().opened }, 3000)

        verify(acceptButton().enabled, "accept was available at once")
        compare(confirmDialog().countdown, 0)
    }

    // ── One button, four meanings ─────────────────────────────────────
    //
    // The same control starts a write, cancels one, skips verification and
    // moves on when it is done, choosing its label and its action from the
    // write state. They are chosen in two separate places, so nothing but a
    // test stops them drifting -- and a button that says Cancel while doing
    // Write, or the reverse, is about as bad as this application gets.
    //
    // The states reachable from here are Idle, one that counts as writing,
    // and finished. Verifying is set only from inside a running write, so
    // "Skip verification" stays unverified.

    function test_before_a_write_the_button_says_write() {
        compare(step.nextButtonText, qsTr("Write"))
        verify(!step.isWriting)
        verify(!step.isComplete)
    }

    function test_while_writing_the_button_offers_to_cancel() {
        // onFinalizing lands in a state isWriting counts, which is what a
        // write in progress looks like to this step.
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return step.isWriting }, 3000, "the step sees a write")

        compare(step.nextButtonText, qsTr("Cancel write"))

        ImageWriterSingleton.onCancelled()
    }

    function test_while_writing_the_button_actually_cancels() {
        // The other half. Pressing it must reach cancelWrite() rather than
        // starting a second write over the top of the first.
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return step.isWriting }, 3000)

        step.nextClicked()

        // Cancelling is the state while a thread winds down. With nothing
        // running there is nothing to wait for, so it completes straight to
        // Cancelled; both mean the button cancelled rather than started.
        tryVerify(function () {
            var s = ImageWriterSingleton.writeState
            return s === ImageWriterSingleton.Cancelling || s === ImageWriterSingleton.Cancelled
        }, 3000, "the write was cancelled")
        verify(!confirmDialog().opened, "and no erase confirmation was raised")

        ImageWriterSingleton.onCancelled()
    }

    function test_when_the_write_is_done_the_button_moves_on() {
        // Via a writing state: onSuccess is deliberately ignored once a run
        // has failed or been cancelled, which is the guard tested in
        // image_writer_test.
        ImageWriterSingleton.onFinalizing()
        ImageWriterSingleton.onSuccess()
        tryVerify(function () { return step.isComplete }, 3000, "the step sees it finished")

        compare(step.nextButtonText, CommonStrings.continueText)

        var before = fakeContainer.steps
        step.nextClicked()

        compare(fakeContainer.steps, before + 1, "it advanced rather than writing again")
        verify(!confirmDialog().opened, "and raised no confirmation")

        ImageWriterSingleton.onCancelled()
    }

    function test_a_finished_write_is_not_offered_a_cancel() {
        // Idle and finished both leave isWriting false, but they must not
        // produce the same label -- one starts a write and one leaves.
        ImageWriterSingleton.onFinalizing()
        ImageWriterSingleton.onSuccess()
        tryVerify(function () { return step.isComplete }, 3000)

        verify(step.nextButtonText !== qsTr("Cancel write"))
        verify(step.nextButtonText !== qsTr("Write"))

        ImageWriterSingleton.onCancelled()
    }
}
