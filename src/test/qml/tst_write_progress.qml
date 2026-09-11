/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The progress a user watches while their card is being erased.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "WriteProgress"
    when: windowShown
    width: 900
    height: 700
    visible: true

    Component {
        id: containerComponent
        WizardContainer {}
    }

    property var wiz: null

    function initTestCase() {
        wiz = containerComponent.createObject(testCase)
        verify(wiz, "the wizard container was created")
        // main.qml supplies this; the steps parent their dialogs onto it.
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
        wiz.selectedDeviceName = "Raspberry Pi 5"
        wiz.selectedOsName = "Raspberry Pi OS (64-bit)"
        wiz.selectedStorageName = "Generic Mass-Storage 32 GB"
        wiz.jumpToStep(wiz.stepWriting)
        compare(wiz.currentStep, wiz.stepWriting, "the writing step is showing")
    }

    function cleanup() {
        // Leave no write running for the next case, or for the next file.
        ImageWriterSingleton.onCancelled()
        tryVerify(function () { return !writingStep().isWriting }, 3000)
    }

    // The stack is an id inside the container rather than an exposed property.
    function findStack(item) {
        if (!item)
            return null
        if (item.currentItem !== undefined && item.depth !== undefined)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findStack(kids[i])
            if (found)
                return found
        }
        return null
    }

    function writingStep() {
        const stack = findStack(wiz)
        verify(stack !== null, "the container has a step stack")
        return stack.currentItem
    }

    function bar() {
        const b = findChild(wiz, "writeProgressBar")
        verify(b, "the progress bar was found")
        return b
    }

    function statusText() {
        const t = findChild(wiz, "writeProgressText")
        verify(t, "the progress text was found")
        return t
    }

    function startWriting() {
        // onFinalizing lands in a state the step counts as a write in
        // progress, which is what the guards below turn on.
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return writingStep().isWriting }, 3000,
                  "the step sees a write running")
    }

    // -- Progress reaches the bar ------------------------------------------

    function test_write_progress_reaches_the_bar_and_the_number() {
        startWriting()

        wiz.onWriteProgress(50, 100)

        compare(bar().value, 50, "the bar shows how much has been written")
        compare(String(statusText().text), "Writing... 50%",
                "and the text says the same")
    }

    function test_verification_progress_replaces_the_write_progress() {
        // Verification is a second pass over the same card, so it restarts the
        // bar. Saying "Writing" through it would have the user think the write
        // had gone backwards.
        startWriting()
        wiz.onWriteProgress(100, 100)

        wiz.onVerifyProgress(30, 100)

        compare(bar().value, 30)
        compare(String(statusText().text), "Verifying... 30%",
                "and it says which pass it is on")
    }

    function test_a_preparation_message_is_shown_as_given() {
        // These are sentences from the writer -- unmounting, zeroing -- shown
        // before there is any percentage to report.
        startWriting()

        wiz.onPreparationStatusUpdate("Zeroing out first and last MB of drive")

        compare(String(statusText().text),
                "Zeroing out first and last MB of drive")
    }

    function test_progress_of_an_unknown_length_does_not_divide_by_zero() {
        // A compressed stream whose uncompressed size is not known reports a
        // total of zero. now/total would be Infinity, and the bar would be
        // handed a value it cannot render.
        startWriting()

        wiz.onWriteProgress(12345, 0)

        compare(bar().value, 0, "the bar stays at zero rather than going wild")
        verify(isFinite(bar().value), "and holds a real number")
        compare(String(statusText().text), "Writing... 0%")

        wiz.onVerifyProgress(12345, 0)
        compare(bar().value, 0)
        verify(isFinite(bar().value))
    }

    function test_an_indeterminate_write_reports_megabytes_instead() {
        // With no total to divide by, the step reports what it does know.
        startWriting()
        writingStep().isIndeterminateProgress = true

        wiz.onWriteProgress(3 * 1024 * 1024, 0)

        compare(String(statusText().text), "Writing... 3 MB written",
                "the user gets a number that means something")
        writingStep().isIndeterminateProgress = false
    }

    // -- The step's guard: no write, no movement ---------------------------

    function test_progress_arriving_after_the_write_does_not_move_the_bar() {
        // The signals come off a thread that is winding down, so one can land
        // after the write is over. A bar that jumps back to 40% on the
        // completion screen says the card is still being written when it is
        // safe to unplug.
        startWriting()
        wiz.onWriteProgress(100, 100)
        compare(bar().value, 100)

        ImageWriterSingleton.onCancelled()
        tryVerify(function () { return !writingStep().isWriting }, 3000)

        wiz.onWriteProgress(40, 100)
        compare(bar().value, 100, "a late signal is ignored")

        wiz.onVerifyProgress(40, 100)
        compare(bar().value, 100, "and so is a late verification signal")

        wiz.onPreparationStatusUpdate("Unmounting")
        verify(String(statusText().text) !== "Unmounting",
               "and so is a late status message")
    }

    // -- The container's guard: not on this step, nothing forwarded --------

    function test_progress_arriving_on_another_step_is_dropped() {
        // The forwarding functions call onWriteProgress on whatever step is
        // current. On any step but the writing one there is no such method, so
        // the call has to be guarded rather than attempted.
        startWriting()
        wiz.onWriteProgress(70, 100)
        compare(bar().value, 70)

        wiz.jumpToStep(wiz.stepDone)
        compare(wiz.currentStep, wiz.stepDone)

        // None of these may throw, and none may reach anything.
        wiz.onWriteProgress(10, 100)
        wiz.onVerifyProgress(10, 100)
        wiz.onDownloadProgress(10, 100)
        wiz.onPreparationStatusUpdate("Unmounting")
        wiz.onFinalizing()

        compare(wiz.currentStep, wiz.stepDone,
                "the wizard stays where the user left it")
    }

    // -- Cancelling brings the user back to see what happened --------------

    function test_a_cancelled_write_brings_the_user_back_to_the_write_step() {
        // Cancelling can be triggered from outside the step -- a removed
        // device, or the window being closed -- so the user may be elsewhere
        // when it lands. They have to be brought back to the step that
        // explains what happened rather than left where they were.
        wiz.jumpToStep(wiz.stepUserCustomization)
        compare(wiz.currentStep, wiz.stepUserCustomization)

        wiz.onWriteCancelled()

        compare(wiz.currentStep, wiz.stepWriting,
                "a cancelled write shows the write step and its summary")
    }

    function test_a_cancelled_write_on_the_write_step_stays_put() {
        // Already there: no second navigation, which would rebuild the step
        // and throw away what it was showing.
        compare(wiz.currentStep, wiz.stepWriting)
        const before = writingStep()

        wiz.onWriteCancelled()

        compare(wiz.currentStep, wiz.stepWriting)
        compare(writingStep(), before,
                "the step is not torn down and rebuilt underneath the user")
    }

    // -- Download progress -------------------------------------------------

    function test_download_progress_is_accepted_and_deliberately_not_shown() {
        // The step takes it and does nothing with it: the write progress is
        // the honest one, because it reflects bytes that reached the card
        // rather than bytes that reached memory. What matters is that it is
        // not mistaken for write progress and does not move the bar.
        startWriting()
        wiz.onWriteProgress(25, 100)
        compare(bar().value, 25)

        wiz.onDownloadProgress(90, 100)

        compare(bar().value, 25,
                "downloading is not writing, and the bar tracks the card")
        compare(String(statusText().text), "Writing... 25%")
    }
}
