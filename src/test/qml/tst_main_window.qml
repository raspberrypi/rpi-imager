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

    // ── Being asked for the keychain ──────────────────────────────────
    //
    // The keychain holds the saved Wi-Fi key and the password hash, so
    // being read from it is worth a prompt. Two things matter about how
    // that prompt behaves: nothing is answered until the user answers it,
    // and dismissing it counts as a refusal. A prompt that reads as consent
    // when waved away is worse than no prompt at all.

    function keychainDialog() {
        var d = findChild(win, "keychainPermissionDialog")
        verify(d, "found the keychain prompt")
        return d
    }

    SignalSpy { id: answered; signalName: "keychainPermissionResponseReceived" }
    SignalSpy { id: granted; signalName: "accepted" }
    SignalSpy { id: refused; signalName: "rejected" }

    function armKeychainSpies() {
        answered.target = ImageWriterSingleton
        answered.clear()
        granted.target = keychainDialog()
        granted.clear()
        refused.target = keychainDialog()
        refused.clear()
    }

    function test_being_asked_for_the_keychain_raises_a_prompt() {
        container().disableWarnings = false
        armKeychainSpies()

        win.onKeychainPermissionRequested()

        tryVerify(function () { return keychainDialog().opened }, 3000,
                  "the user was asked")
        compare(answered.count, 0,
                "and nothing was answered on their behalf")

        keychainDialog().reject()
        tryVerify(function () { return !keychainDialog().visible }, 3000)
    }

    function test_dismissing_the_prompt_is_a_refusal() {
        // The one that matters. Escape, or clicking away, must not be read
        // as permission to open the keychain.
        container().disableWarnings = false
        armKeychainSpies()
        win.onKeychainPermissionRequested()
        tryVerify(function () { return keychainDialog().opened }, 3000)

        keychainDialog().escapePressed()

        tryVerify(function () { return refused.count === 1 }, 3000,
                  "it was refused")
        compare(granted.count, 0, "and certainly not granted")
        verify(!keychainDialog().userAccepted)
    }

    function test_accepting_the_prompt_grants_it() {
        // The counterpart, so the case above is not just "nothing is ever
        // granted".
        container().disableWarnings = false
        armKeychainSpies()
        win.onKeychainPermissionRequested()
        tryVerify(function () { return keychainDialog().opened }, 3000)

        keychainDialog().accept()

        tryVerify(function () { return granted.count === 1 }, 3000)
        compare(refused.count, 0)
        tryVerify(function () { return answered.count === 1 }, 3000,
                  "and the answer reached the writer")
    }

    function test_with_warnings_off_the_keychain_is_granted_unasked() {
        // The documented behaviour of the deployment-wide opt-out. Worth
        // pinning because it is the one path that answers for the user, so
        // it must be reached only by that setting and must actually answer
        // rather than leaving the request hanging.
        container().disableWarnings = true
        armKeychainSpies()

        win.onKeychainPermissionRequested()

        tryVerify(function () { return answered.count === 1 }, 3000,
                  "the request was answered")
        verify(!keychainDialog().opened, "without asking")

        container().disableWarnings = false
    }


    // ── From the writer to the screen ─────────────────────────────────
    //
    // C++ invokes these methods on the window by name, and each hands its
    // value to the wizard, which hands it to the writing step.
    // tst_write_progress covers the second hop by calling the container
    // directly, which left the hop from the window -- the only route any of
    // this takes -- with nothing exercising it.
    //
    // A relay lost there is a progress bar that never moves while the card
    // is being written and no error to explain it: the user is left with a
    // screen that looks stuck through a ten-minute write, and the usual
    // response to that is to pull the card out.

    function onWritingStep() {
        for (var i = container().stepDeviceSelection;
                 i <= container().stepDone; i++)
            container().markStepPermissible(i)
        container().selectedDeviceName = "Raspberry Pi 5"
        container().selectedOsName = "Raspberry Pi OS (64-bit)"
        container().selectedStorageName = "Generic Mass-Storage 32 GB"
        container().jumpToStep(container().stepWriting)
        compare(container().currentStep, container().stepWriting,
                "the writing step is showing")
        // onFinalizing lands in a state the step counts as a write running,
        // which is what the progress display is gated on.
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () {
            var s = findChild(win, "writingStep")
            return s && s.isWriting
        }, 3000, "the step sees a write running")
    }

    function progressBar() {
        var b = findChild(win, "writeProgressBar")
        verify(b, "the progress bar was found")
        return b
    }

    function progressText() {
        var t = findChild(win, "writeProgressText")
        verify(t, "the progress text was found")
        return t
    }

    function test_progress_reaches_the_screen_data() {
        return [
            { tag: "writing",   call: "onWriteProgress",
              now: 55, total: 100, says: "55%" },
            { tag: "verifying", call: "onVerifyProgress",
              now: 70, total: 100, says: "70%" }
        ]
    }

    function test_progress_reaches_the_screen(data) {
        onWritingStep()

        win[data.call](data.now, data.total)

        compare(progressBar().value, data.now,
                data.tag + " moves the bar")
        verify(String(progressText().text).indexOf(data.says) !== -1,
               "and the text says how far: got \"" + progressText().text
               + "\", wanted " + data.says)
    }

    function test_download_progress_deliberately_does_not_move_the_bar() {
        // It relays like the others -- the window is where C++ calls it --
        // but the writing step drops it on purpose: the bar reports bytes
        // actually on the card, which is the number the user needs before
        // pulling it out, and a download that ran ahead of the write would
        // have the bar go backwards.
        //
        // So this pins the intent, not the relay. Emptying the relay leaves
        // this case passing, because both ends of it do nothing -- measured.
        // What it would catch is the writing step starting to draw download
        // progress on the same bar.
        onWritingStep()
        win.onWriteProgress(30, 100)
        compare(progressBar().value, 30, "the bar is showing the write")

        win.onDownloadProgress(90, 100)

        compare(progressBar().value, 30,
                "the download did not overwrite it")
    }

    function test_the_status_message_reaches_the_screen() {
        // What is shown before there is any progress to show -- unmounting,
        // hashing, waiting on the card. Without it the screen says nothing
        // at all for the first part of a write.
        onWritingStep()

        win.onPreparationStatusUpdate("Unmounting drive")

        compare(String(progressText().text), "Unmounting drive")
    }

    function test_finalising_reaches_the_screen() {
        // The last phase, where the data is on its way out of the cache and
        // the card must not be pulled out. The screen has to say so.
        //
        // The bar is driven off 100 first: the setup gets the step into a
        // writing state through the writer's own finalizing signal, which
        // already puts it at 100, so an assertion made straight after that
        // would hold whether this relay arrived or not.
        onWritingStep()
        win.onWriteProgress(30, 100)
        compare(progressBar().value, 30, "the bar is somewhere else first")

        win.onFinalizing()

        compare(progressBar().value, 100, "the write is complete")
        compare(String(progressText().text), "Finalising...",
                "and the screen says what is still happening")
    }

    function test_a_cancelled_write_puts_the_user_back_on_the_write_screen() {
        // Cancelling can arrive while the user is somewhere else -- they
        // walked back through the sidebar. The summary of what happened is
        // on the writing step, so that is where they have to be to see it.
        onWritingStep()
        container().jumpToStep(container().stepStorageSelection)
        compare(container().currentStep, container().stepStorageSelection)

        win.onCancelled()

        compare(container().currentStep, container().stepWriting,
                "back on the screen that says what happened")
    }

    function test_network_details_are_kept_off_the_desktop_build() {
        // The network banner exists for the embedded build, where there is
        // no desktop to show connection state. On a desktop it would be a
        // second, possibly disagreeing, copy of what the system already
        // shows.
        if (ImageWriterSingleton.isEmbeddedMode()) {
            skip("this is an embedded build, where the banner is wanted")
            return
        }
        container().networkInfoText = "set by something else"

        win.onNetworkInfo("192.168.1.50 -- wlan0")

        compare(container().networkInfoText, "set by something else",
                "the banner was left alone")
    }

    // ── Reaching the application options ──────────────────────────────
    //
    // The gear button is on the first two steps and is the only way to the
    // options. The button raises a signal on the wizard; the window is what
    // creates the dialog and opens it. That hop was uncovered, so the gear
    // doing nothing at all would not have failed a test.

    function appOptions() {
        var d = findChild(win, "appOptionsDialog")
        verify(d, "found the options dialog")
        return d
    }

    function test_asking_for_the_options_opens_them() {
        container().appOptionsRequested()

        tryVerify(function () {
            var d = findChild(win, "appOptionsDialog")
            return d && d.opened
        }, 5000, "the options dialog came up")

        appOptions().close()
        tryVerify(function () { return !appOptions().visible }, 3000)
    }

    function test_finishing_the_wizard_returns_to_the_beginning() {
        // "Write another" comes back through here. Left where it was, the
        // user would be looking at the completion screen of the card they
        // just wrote.
        container().currentStep = container().stepDone

        container().wizardCompleted()

        compare(container().currentStep, 0,
                "back at the start, ready for the next card")
    }

    function test_the_debug_shortcut_opens_the_debug_options() {
        // Ctrl+Alt+S, and deliberately undocumented. Worth a case because
        // of how it is written: the dialog is behind a Loader that is only
        // switched on here, and initialize() is called on the item in the
        // same breath. If that ever stopped being available immediately the
        // shortcut would throw instead of opening anything.
        keyClick(Qt.Key_S, Qt.ControlModifier | Qt.AltModifier)

        tryVerify(function () {
            var d = findChild(win, "debugOptionsDialog")
            return d && d.opened
        }, 5000, "the debug options came up")

        var dlg = findChild(win, "debugOptionsDialog")
        dlg.close()
        tryVerify(function () { return !dlg.visible }, 3000)
    }


    // ── Answering the keychain prompt with its own buttons ────────────
    //
    // The cases above call accept() and reject() on the dialog. Its two
    // buttons do something first: they set the flag that onClosed reads to
    // decide whether an unanswered dialog counts as a refusal. Neither
    // button had ever been pressed.
    //
    // Which means the shape those handlers protect against was untested. Go
    // through accept() without setting the flag, as those cases do, and the
    // dialog reports a grant and then, once it has finished closing,
    // a refusal as well -- two contradicting answers to one request, and
    // the writer acts on the last one. The buttons are what stop that, so
    // these press them and then wait for the dialog to be fully gone before
    // counting.

    function keychainButton(name) {
        var b = findChild(keychainDialog(), name)
        verify(b, "found " + name)
        return b
    }

    // Note on the Yes case: with the answered flag in place, removing
    // `userAccepted = true` from the Yes button fails nothing, because the
    // flag is what now keeps onClosed quiet. userAccepted is still read
    // from outside -- the escape case above checks it -- so it is not dead,
    // but it no longer carries this path on its own.
    function test_pressing_yes_grants_it_once_and_only_once() {
        container().disableWarnings = false
        armKeychainSpies()
        win.onKeychainPermissionRequested()
        tryVerify(function () { return keychainDialog().opened }, 3000)

        keychainButton("keychainYesButton").clicked()

        tryVerify(function () { return !keychainDialog().visible }, 3000,
                  "the prompt is gone")
        wait(200)
        compare(granted.count, 1, "granted")
        compare(refused.count, 0,
                "and not also refused on the way out")
        compare(answered.count, 1, "one answer reached the writer")
    }

    function test_pressing_no_refuses_it_once_and_only_once() {
        container().disableWarnings = false
        armKeychainSpies()
        win.onKeychainPermissionRequested()
        tryVerify(function () { return keychainDialog().opened }, 3000)

        keychainButton("keychainNoButton").clicked()

        tryVerify(function () { return !keychainDialog().visible }, 3000)
        wait(200)
        compare(refused.count, 1, "refused")
        compare(granted.count, 0)
        compare(answered.count, 1)
    }

    function test_escape_refuses_it_once_and_only_once() {
        // Same shape as the No button, and the same fix: escape refuses,
        // and the catch-all behind it must not refuse again.
        container().disableWarnings = false
        armKeychainSpies()
        win.onKeychainPermissionRequested()
        tryVerify(function () { return keychainDialog().opened }, 3000)

        keychainDialog().escapePressed()

        tryVerify(function () { return !keychainDialog().visible }, 3000)
        wait(200)
        compare(refused.count, 1)
        compare(granted.count, 0)
        compare(answered.count, 1)
    }

    function test_a_prompt_that_goes_away_unanswered_is_a_refusal() {
        // The case the catch-all exists for: closed from outside, with
        // neither button pressed. Silence has to be no.
        container().disableWarnings = false
        armKeychainSpies()
        win.onKeychainPermissionRequested()
        tryVerify(function () { return keychainDialog().opened }, 3000)

        keychainDialog().close()

        tryVerify(function () { return !keychainDialog().visible }, 3000)
        tryVerify(function () { return refused.count === 1 }, 3000,
                  "it was refused on the way out")
        compare(granted.count, 0)
    }

    function test_the_prompt_says_what_each_answer_costs() {
        // On macOS "yes" raises an administrator password prompt from the
        // system, which is a surprising thing to be shown without warning.
        container().disableWarnings = false
        armKeychainSpies()
        win.onKeychainPermissionRequested()
        tryVerify(function () { return keychainDialog().opened }, 3000)

        var yes = keychainButton("keychainYesButton")
        var no = keychainButton("keychainNoButton")
        verify(String(yes.accessibleDescription).length > 0)
        verify(String(no.accessibleDescription).length > 0)
        verify(yes.activeFocusOnTab, "and both are reachable by keyboard")
        verify(no.activeFocusOnTab)

        keychainDialog().reject()
    }


    // ── Dismissing the window's own dialogs ───────────────────────────
    //
    // Both of these are raised over a write in progress, and both had their
    // escape handling uncovered. The quit one is the dangerous half: the
    // window's close button raises it rather than quitting, and if escape
    // there were taken as the answer, closing the window and then pressing
    // escape would abort a write that was half way through a card.

    function test_escape_on_the_quit_question_leaves_the_write_running() {
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () {
            return ImageWriterSingleton.writeState === ImageWriterSingleton.Finalizing
        }, 3000, "a write is in progress")
        win.close()
        tryVerify(function () { return quitDialog().opened }, 3000,
                  "the question was raised instead of quitting")

        quitDialog().escapePressed()

        tryVerify(function () { return !quitDialog().visible }, 3000,
                  "the question went away")
        verify(win.visible, "the window is still up")
        verify(!win.forceQuit, "and nothing was taken as an answer to quit")
        compare(ImageWriterSingleton.writeState,
                ImageWriterSingleton.Finalizing,
                "with the write still going")
    }

    function test_escape_dismisses_the_lost_card_notice() {
        // Told once is enough; it must be possible to get rid of it and
        // carry on with the chooser it put the user back on.
        container().currentStep = container().stepWriting
        container().selectedStorageName = "Generic Mass-Storage"
        win.onSelectedDeviceRemoved()
        tryVerify(function () { return removalDialog().opened }, 3000)

        removalDialog().escapePressed()

        tryVerify(function () { return !removalDialog().visible }, 3000)
    }

    // ── Saving the performance data where there is no native dialog ───
    //
    // The export falls back to a styled save dialog when the platform has
    // no native one. The handler that sets it up had never run, so on those
    // platforms the export offered nothing at all.

    function performanceSave() {
        var d = findChild(win, "performanceSaveDialog")
        verify(d, "found the performance save dialog")
        return d
    }

    function test_the_fallback_save_dialog_opens_where_it_was_told_to() {
        ImageWriterSingleton.performanceSaveDialogNeeded("imager-perf.json",
                                                         "/tmp")

        tryVerify(function () { return performanceSave().opened }, 3000,
                  "the save dialog came up")
        compare(performanceSave().suggestedFilename, "imager-perf.json",
                "with the name it was given")
        verify(String(performanceSave().currentFolder).indexOf("/tmp") !== -1,
               "and the folder; got " + performanceSave().currentFolder)

        performanceSave().close()
        tryVerify(function () { return !performanceSave().visible }, 3000)
    }

    // ── The export shortcut ───────────────────────────────────────────

    function test_asking_to_export_with_nothing_recorded_does_nothing() {
        // Ctrl+Shift+P. With no write behind it there is nothing to write
        // out, and the shortcut has to say so rather than producing an empty
        // file the user then has to make sense of.
        // Skipped rather than pressed when there is data, and deliberately:
        // the other branch asks the platform for a save dialog, and where
        // there is a native one that is a modal window with nobody to close
        // it. The run stops there.
        if (ImageWriterSingleton.hasPerformanceData()) {
            skip("this session has performance data, so pressing the "
                 + "shortcut would open a native save dialog and hang")
            return
        }

        keyClick(Qt.Key_P, Qt.ControlModifier | Qt.ShiftModifier)

        wait(200)
        verify(!performanceSave().visible,
               "no save dialog was raised for data that is not there")
    }

    // ── Where the images came from ────────────────────────────────────
    //
    // Pointing Imager at a custom repository changes what it will offer to
    // write, and the window title is the only place that is said. The OS
    // list itself looks identical whoever served it, so a user handed a
    // machine already pointed somewhere else has nothing else to go on --
    // which is why the code that follows a redirect calls this a security
    // consideration rather than a nicety.

    function test_the_title_says_which_repository_the_list_came_from() {
        const repo = TestFiles.write("main_window_repo.json",
                                     JSON.stringify({ "os_list": [] }))
        verify(repo !== "", "wrote a repository file")

        ImageWriterSingleton.refreshOsListFrom(repo)

        tryVerify(function() { return win.customRepoHost.length > 0 }, 5000,
                  "the window noticed the repository changed")
        // A local file is named by its filename rather than a host, because
        // "file" would tell the user nothing.
        compare(win.customRepoHost, "main_window_repo.json")
        verify(win.title.indexOf("Using data from") >= 0,
               "the title says the list is not the usual one: " + win.title)
        verify(win.title.indexOf("main_window_repo.json") >= 0,
               "and names it: " + win.title)

        // Back to the shipped list, which also has to clear the notice --
        // a title still claiming a custom repository is worse than never
        // having shown one.
        ImageWriterSingleton.refreshOsListFromDefaultUrl()
        tryVerify(function() { return win.customRepoHost.length === 0 }, 10000,
                  "the notice went away with the repository")
        verify(win.title.indexOf("Using data from") < 0,
               "and the title is back to plain: " + win.title)
    }

    function test_the_title_follows_the_repository_moving() {
        // A custom repository that answers with a redirect is served by a
        // host the user never typed, and the title has to name the one that
        // actually answered. The writer does that by replacing the stored
        // repository with the final URL and saying the host changed; there
        // is no seam here to redirect a real fetch through -- a redirect
        // needs a server, and the harness has none -- so the two halves are
        // done directly, in the same order.
        const first = TestFiles.write("moved_from.json",
                                      JSON.stringify({ "os_list": [] }))
        ImageWriterSingleton.refreshOsListFrom(first)
        tryVerify(function() {
            return win.customRepoHost === "moved_from.json"
        }, 5000, "the first repository is in the title")

        const second = TestFiles.write("moved_to.json",
                                       JSON.stringify({ "os_list": [] }))
        ImageWriterSingleton.setCustomRepo(second)
        ImageWriterSingleton.customRepoHostChanged()

        compare(win.customRepoHost, "moved_to.json",
                "the title names the repository that answered")
        verify(win.title.indexOf("moved_to.json") >= 0, win.title)

        ImageWriterSingleton.refreshOsListFromDefaultUrl()
        tryVerify(function() { return win.customRepoHost.length === 0 }, 10000)
    }
}
