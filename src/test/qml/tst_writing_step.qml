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
        // The summary lists every interface the image will enable, so the
        // step reads all of them. Missing, they resolve to undefined and the
        // rows silently do not render -- which no assertion here would catch.
        property bool ifI2cEnabled: false
        property bool if1WireEnabled: false
        property bool ifSpiEnabled: false
        property string ifSerial: ""
        // WizardStepBase reads this on every step; undefined assigns nothing
        // and warns on each construction.
        property string networkInfoText: ""
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
        // Leave no write running whatever happened. A case that fails
        // part-way through would otherwise hand the next one a writer that
        // is still busy, and the failure would appear to be somewhere else
        // -- which is exactly what happened while these were being checked.
        ImageWriterSingleton.onCancelled()
        // The summary cases turn every customisation on; left set, the rows
        // they add change what the other cases are looking at.
        fakeContainer.hostnameConfigured = false
        fakeContainer.localeConfigured = false
        fakeContainer.userConfigured = false
        fakeContainer.wifiConfigured = false
        fakeContainer.sshEnabled = false
        fakeContainer.piConnectEnabled = false
        fakeContainer.featUsbGadgetEnabled = false
        fakeContainer.ifI2cEnabled = false
        fakeContainer.ifSpiEnabled = false
        fakeContainer.if1WireEnabled = false
        fakeContainer.ifSerial = ""
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

    // ── What the screen says while it runs, and afterwards ────────────
    //
    // These handlers are the only account the user gets of a write. The
    // guard on them is the part worth pinning: every one checks isWriting
    // first, so a progress signal that arrives after the write has ended
    // cannot paint over the outcome. Without it a trailing update replaces
    // "Write failed: no space left on device" with "Writing... 50%", and
    // the reason is gone from the one place it was shown.

    function progressText() {
        var t = findChild(step, "writeProgressText")
        verify(t, "found the progress text")
        return t
    }

    function progressBar() {
        var b = findChild(step, "writeProgressBar")
        verify(b, "found the progress bar")
        return b
    }

    function beWriting() {
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return step.isWriting }, 3000, "a write is under way")
    }

    function test_progress_is_shown_as_a_percentage() {
        beWriting()

        step.onWriteProgress(512, 1024)

        compare(progressBar().value, 50)
        verify(progressText().text.indexOf("50") !== -1,
               "the figure is on screen: " + progressText().text)

        ImageWriterSingleton.onCancelled()
    }

    function test_progress_without_a_known_total_shows_what_has_been_written() {
        // A gzip stream over 4 GB does not report its size, so a percentage
        // would sit at zero and read as stuck. Bytes written at least moves.
        beWriting()
        step.isIndeterminateProgress = true

        step.onWriteProgress(3 * 1024 * 1024, 0)

        verify(progressText().text.indexOf("3") !== -1,
               "megabytes written are shown: " + progressText().text)
        verify(progressText().text.indexOf("%") === -1,
               "and no percentage is invented")

        step.isIndeterminateProgress = false
        ImageWriterSingleton.onCancelled()
    }

    function test_verification_says_it_is_verifying() {
        // Distinct from writing, because it takes about as long again and a
        // user told "Writing... 100%" for minutes assumes it has hung.
        beWriting()

        step.onVerifyProgress(256, 1024)

        compare(progressBar().value, 25)
        verify(progressText().text.toLowerCase().indexOf("verif") !== -1,
               progressText().text)

        ImageWriterSingleton.onCancelled()
    }

    function test_verification_clears_a_warning_from_the_write() {
        // A warning about the write phase -- a sync fallback, say -- is not
        // true of verification and must not be left on screen beside it.
        beWriting()
        step.operationWarning = "the write fell back to synchronous I/O"

        step.onVerifyProgress(1, 2)

        compare(step.operationWarning, "")

        ImageWriterSingleton.onCancelled()
    }

    function test_a_preparation_message_is_passed_through() {
        beWriting()

        step.onPreparationStatusUpdate("Checking the cached image")

        compare(progressText().text, "Checking the cached image")

        ImageWriterSingleton.onCancelled()
    }

    // ── The outcome, and nothing painting over it ────────────────────

    function test_a_failure_says_why() {
        // "Write failed:" with nothing after it leaves the user with no
        // reason and nothing to search for.
        beWriting()

        ImageWriterSingleton.onError("no space left on device")

        tryVerify(function () {
            return progressText().text.indexOf("no space left on device") !== -1
        }, 3000, "the reason is on screen: " + progressText().text)

        ImageWriterSingleton.onCancelled()
    }

    function test_a_late_progress_signal_does_not_hide_a_failure() {
        // The guard. Progress arriving after the write ended must not
        // replace the failure text with a percentage.
        beWriting()
        ImageWriterSingleton.onError("no space left on device")
        tryVerify(function () {
            return progressText().text.indexOf("no space left on device") !== -1
        }, 3000)

        step.onWriteProgress(512, 1024)
        step.onVerifyProgress(512, 1024)
        step.onPreparationStatusUpdate("something else entirely")

        verify(progressText().text.indexOf("no space left on device") !== -1,
               "the failure is still what is shown: " + progressText().text)

        ImageWriterSingleton.onCancelled()
    }

    function test_a_warning_from_the_write_is_carried_to_the_screen() {
        beWriting()

        ImageWriterSingleton.onOperationWarning("the write fell back to synchronous I/O")

        tryVerify(function () {
            return step.operationWarning === "the write fell back to synchronous I/O"
        }, 3000)

        ImageWriterSingleton.onCancelled()
    }

    // ── The end of the write, and what the bar says while it happens ──
    //
    // Finalising is the sync: everything written is being flushed to the card,
    // and on a slow card it takes a while with no more bytes to count. So the
    // bar goes to 100 and the text says what is happening -- a bar sitting at
    // 97% through a long sync reads as a stalled write, and a user who pulls
    // the card then has a half-written one.

    function test_finalising_fills_the_bar_and_says_so() {
        beWriting()
        // beWriting() gets there through onFinalizing(), which has already
        // filled the bar -- so it is put back part-way first, or the assertion
        // below holds whether or not the call does anything.
        progressBar().value = 40
        progressText().text = "part-way"

        step.onFinalizing()

        compare(progressBar().value, 100,
                "nothing is left to count, so the bar is full")
        compare(String(progressText().text), "Finalising...",
                "and the text says why it is sitting there")

        ImageWriterSingleton.onCancelled()
    }

    function test_the_writer_saying_it_is_finalising_reaches_the_screen() {
        // The other route to the same state: the step listens for the signal
        // as well as being called directly by the container.
        beWriting()

        ImageWriterSingleton.onFinalizing()

        tryVerify(function () {
            return String(progressText().text) === "Finalising..."
        }, 3000, "the writer's own signal says it too: "
                 + progressText().text)
        compare(progressBar().value, 100)

        ImageWriterSingleton.onCancelled()
    }

    function test_every_route_to_finalising_says_the_same_thing() {
        // Three places set this text -- the direct call, the signal handler,
        // and cancelling a write -- and they used to disagree by an ellipsis:
        // "Finalising..." against "Finalising…". That made one user-visible
        // state into two translatable strings, translators duly did both, and
        // German ended up with "Finalisiere..." for one and "Finalisiere...."
        // for the other.
        //
        // Comparing the routes against each other rather than against a
        // literal, so this fails if they diverge again whatever wording is
        // chosen.
        beWriting()
        progressText().text = "part-way"
        step.onFinalizing()
        const fromTheCall = String(progressText().text)
        verify(fromTheCall !== "part-way", "the direct call set the text")

        progressText().text = "cleared"
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () {
            return String(progressText().text) !== "cleared"
        }, 3000)
        const fromTheSignal = String(progressText().text)

        compare(fromTheSignal, fromTheCall,
                "the signal and the direct call say the same thing")
        verify(fromTheCall.length > 0, "and it is not empty")

        ImageWriterSingleton.onCancelled()
    }

    // ── Starting a write clears the last one's news ───────────────────
    //
    // The step is reused between writes, so a bottleneck message or a
    // throughput figure left over from the previous card would be shown
    // against this one -- a "slow card" warning about a card that was
    // swapped out.

    function test_a_bottleneck_report_reaches_the_step() {
        beWriting()

        ImageWriterSingleton.onBottleneckStatusChanged("Slow SD card", 2048)

        tryVerify(function () {
            return step.bottleneckStatus === "Slow SD card"
        }, 3000, "the bottleneck is carried to the step")
        compare(step.writeThroughputKBps, 2048,
                "with the throughput that goes with it")

        ImageWriterSingleton.onCancelled()
    }

    function test_a_bottleneck_report_replaces_the_previous_one() {
        // It arrives repeatedly as conditions change, so the step has to show
        // the current reading rather than the first.
        beWriting()
        ImageWriterSingleton.onBottleneckStatusChanged("Slow SD card", 2048)
        tryVerify(function () { return step.bottleneckStatus === "Slow SD card" }, 3000)

        ImageWriterSingleton.onBottleneckStatusChanged("", 51200)

        tryVerify(function () { return step.bottleneckStatus === "" }, 3000,
                  "a cleared bottleneck clears the message")
        compare(step.writeThroughputKBps, 51200)

        ImageWriterSingleton.onCancelled()
    }


    // ── Answering the erase confirmation ──────────────────────────────
    //
    // The countdown is covered above. What each answer does was not: the
    // accept button, the cancel button, the escape key, and the short delay
    // between agreeing and the write actually starting.
    //
    // This is the last thing between a user and their card being erased, so
    // the direction that matters is the refusals. Escape is how people
    // dismiss a dialog they did not mean to open, and it must not be the
    // gesture that starts an irreversible write.
    //
    // The write itself is never started here. The accept path arms a timer
    // whose last act is startWrite(); the cases either stop it first, or --
    // where the timer's own body is what is being covered -- leave the
    // writer already busy, which startWrite() refuses at a guard of its own.

    function cancelButton() {
        var b = findChild(step, "confirmWriteCancelButton")
        verify(b, "found the cancel button")
        return b
    }

    function writeDelay() {
        var t = findChild(step, "beginWriteDelayTimer")
        verify(t, "found the delay before the write")
        return t
    }

    function atTheConfirmation() {
        step.nextClicked()
        tryVerify(function () { return confirmDialog().opened }, 3000,
                  "the confirmation was raised")
        tryVerify(function () { return acceptButton().enabled }, 5000,
                  "and the countdown has run")
        verify(!writeDelay().running, "nothing armed yet")
    }

    function test_escape_does_not_start_the_write() {
        atTheConfirmation()

        confirmDialog().escapePressed()

        tryVerify(function () { return !confirmDialog().visible }, 3000,
                  "the question went away")
        verify(!writeDelay().running,
               "and nothing was set in motion by dismissing it")
        verify(!step.isWriting)
    }

    function test_cancel_does_not_start_the_write() {
        atTheConfirmation()

        cancelButton().clicked()

        tryVerify(function () { return !confirmDialog().visible }, 3000)
        verify(!writeDelay().running)
        verify(!step.isWriting)
    }

    function test_agreeing_closes_the_question_and_arms_the_write() {
        // Deliberately not immediate: the write raises an authentication
        // prompt on some platforms, and starting it while the dialog is
        // still closing has that prompt cancelled by the focus change.
        atTheConfirmation()

        acceptButton().clicked()

        verify(writeDelay().running, "the write was armed")
        tryVerify(function () { return !confirmDialog().visible }, 3000,
                  "and the question is out of the way first")

        // Disarmed before it can fire: what happens after it is the case
        // below, which arranges for the write to be refused.
        writeDelay().stop()
    }

    function test_the_armed_write_sets_the_screen_up_for_it() {
        // The delay's own body. It clears what the last write left behind
        // and says what is happening, so the screen is not showing the
        // previous attempt's warning while this one starts.
        //
        // The writer is left busy on purpose: the last thing this does is
        // ask for a write, and a write already in progress is refused by
        // ImageWriter itself. Everything before that still runs.
        step.operationWarning = "something from last time"
        step.bottleneckStatus = "slow reader"
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return step.isWriting }, 3000,
                  "the writer is busy, so the request will be refused")

        writeDelay().triggered()

        compare(step.operationWarning, "", "last time's warning is gone")
        compare(step.bottleneckStatus, "")
        compare(String(progressText().text), "Starting write process...",
                "and the screen says what is happening")
    }

    // ── Reading the summary without a mouse ───────────────────────────

    function test_the_summary_of_what_will_be_written_can_be_scrolled() {
        // The list of everything the image will carry sits in the
        // confirmation, and it is what the user is being asked to agree to.
        // On a short window it scrolls, and a keyboard user has the arrow
        // keys and nothing else.
        fakeContainer.hostnameConfigured = true
        fakeContainer.localeConfigured = true
        fakeContainer.userConfigured = true
        fakeContainer.wifiConfigured = true
        fakeContainer.sshEnabled = true
        fakeContainer.ifI2cEnabled = true
        fakeContainer.ifSpiEnabled = true
        fakeContainer.if1WireEnabled = true
        fakeContainer.piConnectEnabled = true
        fakeContainer.featUsbGadgetEnabled = true
        fakeContainer.ifSerial = "Console"
        atTheConfirmation()

        var f = findChild(step, "writeSummaryFlickable")
        verify(f, "found the summary")
        if (f.contentHeight <= f.height) {
            skip("the summary fits without scrolling at this size, so moving "
                 + "it cannot be told from not moving")
            return
        }
        f.contentY = 0
        f.forceActiveFocus()

        keyClick(Qt.Key_Down)
        verify(f.contentY > 0, "down moved further into the list")

        keyClick(Qt.Key_Up)
        compare(f.contentY, 0, "and up came back to the top")
    }

    function test_the_summary_does_not_scroll_off_either_end() {
        fakeContainer.hostnameConfigured = true
        fakeContainer.userConfigured = true
        atTheConfirmation()

        var f = findChild(step, "writeSummaryFlickable")
        verify(f, "found the summary")
        f.contentY = 0
        f.forceActiveFocus()

        keyClick(Qt.Key_Up)
        keyClick(Qt.Key_Up)

        compare(f.contentY, 0, "the top is the top")

        const maxY = Math.max(0, f.contentHeight - f.height)
        f.contentY = maxY
        for (let i = 0; i < 6; i++)
            keyClick(Qt.Key_Down)

        verify(f.contentY <= maxY + 1,
               "and the bottom is the bottom; contentY " + f.contentY
               + " against " + maxY)
    }

    // ── How the confirmation is laid out ──────────────────────────────

    function test_the_erase_warning_is_not_padded_out_underneath() {
        // The dialog sizes itself to its content, so anything left in the
        // layout below the buttons becomes visible whitespace under them.
        // A spacer had been added here to "balance the dialog's internal top
        // padding" that BaseDialog already applies through its content
        // margins, so the gap under the buttons came to nearly three times
        // the gap above the heading -- a short warning sitting in the top
        // half of a tall box.
        step.nextClicked()
        tryVerify(function () { return confirmDialog().opened }, 3000)
        tryVerify(function () { return confirmDialog().allowAccept }, 6000,
                  "the countdown finished and the buttons appeared")
        waitForRendering(step)

        var d = confirmDialog()
        // The row the buttons sit in, whose y is in the content layout's own
        // coordinates -- the layout is what the dialog's padding is applied
        // to, so measuring in its frame is measuring the padding.
        var buttonRow = acceptButton().parent
        var above = d.contentLayout.y
        var below = d.height - (d.contentLayout.y + buttonRow.y + buttonRow.height)

        verify(above > 0, "there is padding above the content")
        compare(below, above,
                "and the same amount of it under the last row of buttons")
    }
}
