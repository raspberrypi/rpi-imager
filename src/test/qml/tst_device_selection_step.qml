/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * DeviceSelectionStep: the first screen with something on it that can fail.
 *
 * The device list comes off the network, so this screen is the one a user
 * meets when they open the application with no connection: an empty list, a
 * warning, and a Retry button. It was at 30%, and everything about that state
 * was untested.
 *
 * Two things about it matter more than the rest.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "DeviceSelectionStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        // WizardStepBase reads this on every step; undefined assigns nothing
        // and warns on each construction.
        property string networkInfoText: ""
        // The step names the chosen board here. Without the property the
        // assignment fails silently and the selection looks like it never
        // happened.
        property string selectedDeviceName: ""
        property int steps: 0
        function nextStep() { steps++ }
    }

    Component {
        id: stepComponent
        DeviceSelectionStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function init() {
        fakeContainer.steps = 0
        fakeContainer.selectedDeviceName = ""
        step = createTemporaryObject(stepComponent, testCase)
        verify(step !== null, "the step has to instantiate")
    }

    function cleanup() {
        step = null
    }

    // The device list is shared process-wide, so a case has to say which state
    // it is about. The skip message names the reason rather than the symptom,
    // so a reader does not go looking for a broken test.
    function requireNoList() {
        if (!ImageWriterSingleton.isOsListUnavailable) {
            skip("an earlier file in this suite fetched the device list, and it "
                 + "cannot be emptied again without another network fetch; run "
                 + "this file on its own to cover the offline screen")
            return false
        }
        return true
    }

    function requireList() {
        if (ImageWriterSingleton.isOsListUnavailable) {
            skip("no device list was fetched in this run, so there is no "
                 + "populated list to check; the suite as a whole covers this")
            return false
        }
        return true
    }

    function findByText(root, wanted) {
        if (!root)
            return null
        if (root.text !== undefined && String(root.text) === wanted)
            return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findByText(kids[i], wanted)
            if (found)
                return found
        }
        return null
    }

    // -- The state this screen comes up in when there is no network --------

    function test_an_offline_user_is_not_trapped_on_the_first_screen() {
        if (!requireNoList())
            return
        // There is nothing to select, so a Next gated on a selection would
        // leave no way forward at all -- and a custom image does not need a
        // device chosen.
        compare(step.hwlist.count, 0, "no list means nothing to choose from")
        verify(!step.hasDeviceSelected, "nothing can have been selected")
        verify(step.nextButtonEnabled,
               "so Next has to be available anyway, or an offline user cannot "
               + "reach the custom-image route")
    }

    function test_the_screen_says_why_it_is_empty_and_offers_a_way_back() {
        if (!requireNoList())
            return
        const warning = findByText(step, "Unable to load device list")
        verify(warning !== null && warning.visible,
               "an empty list is explained rather than just empty")

        const retry = findByText(step, "Retry")
        verify(retry !== null && retry.visible,
               "and there is a way to try again without restarting")
        verify(retry.enabled)
        verify(String(retry.accessibleDescription).length > 0,
               "described, because a user who cannot see the warning still "
               + "needs to find the button under it")
    }

    function test_the_empty_list_is_hidden_rather_than_shown_empty() {
        if (!requireNoList())
            return
        // The placeholder and the list are mutually exclusive: showing both
        // would put a warning above an empty scroll area.
        verify(step.hwlist.visible === false,
               "the list stands down while the placeholder is up")
    }

    function test_the_list_tells_a_screen_reader_it_is_empty() {
        if (!requireNoList())
            return
        // Sighted users see the placeholder instead. Someone on a screen
        // reader who reaches the list needs it to say there is nothing in it,
        // rather than to announce an unlabelled list.
        const name = String(step.hwlist.accessibleName)
        verify(name.indexOf("No devices") >= 0,
               "an empty device list says so: " + name)
    }

    // -- And the state where the list did arrive ---------------------------

    function test_a_populated_list_replaces_the_warning() {
        if (!requireList())
            return
        verify(step.hwlist.count > 0, "there are devices to choose from")
        verify(step.hwlist.visible, "so the list is what is shown")

        // The placeholder is still in the object tree either way -- it is
        // hidden rather than destroyed -- so what matters is that it is not
        // on the screen. Checking only that the text exists would pass with
        // the warning sitting on top of a working list.
        const warning = findByText(step, "Unable to load device list")
        verify(warning === null || !warning.visible,
               "and the failure warning is not left showing over it")
        const retry = findByText(step, "Retry")
        verify(retry === null || !retry.visible,
               "nor is the retry button")
    }

    function test_a_populated_list_counts_itself_for_a_screen_reader() {
        if (!requireList())
            return
        // The count is read out, so someone who cannot see the list knows how
        // far it goes before starting to arrow through it.
        const name = String(step.hwlist.accessibleName)
        const count = step.hwlist.count
        const expected = count === 1 ? "1 device" : count + " devices"
        verify(name.indexOf(expected) >= 0,
               "the list says how many devices it holds; expected \""
               + expected + "\" in \"" + name + "\"")
    }

    function test_with_a_list_present_next_waits_for_a_choice() {
        if (!requireList())
            return
        // The offline exemption must not leak into the normal case: with
        // devices on offer, moving on without picking one would carry an
        // unset device into the OS list and offer the wrong images.
        step.hwlist.currentIndex = -1
        verify(!step.hasDeviceSelected)
        verify(!step.nextButtonEnabled,
               "a device has to be chosen when there are devices to choose")
    }

    function test_a_refresh_arriving_does_not_reload_a_list_already_full() {
        if (!requireList())
            return
        // The counterpart of the forced reload below. Reloading resets the
        // list, and the comment in the source says why that is avoided: it
        // throws away the scroll position. A user part-way down the list
        // choosing their board must not have it jump back to the top because
        // a refresh landed.
        step.modelLoaded = true

        ImageWriterSingleton.osListPrepared()

        verify(step.modelLoaded,
               "a list that already has rows is left alone")
    }

    // -- Coming back when the list finally arrives -------------------------

    function test_a_list_arriving_after_giving_up_forces_a_reload() {
        if (!requireNoList())
            return
        // The case the handler exists for: the model was marked loaded, but
        // while offline, so it holds no rows. When the list arrives the screen
        // has to drop that "loaded" mark and reload, or it ignores the list
        // and leaves the user looking at nothing with a working connection.
        step.modelLoaded = true
        compare(step.hwlist.count, 0, "loaded, but with nothing in it")

        ImageWriterSingleton.osListPrepared()

        // The reload is attempted and, with still no data behind it, fails --
        // so the mark stays off. What must not happen is the mark surviving,
        // because then no later arrival would be picked up either.
        verify(!step.modelLoaded,
               "the stale 'loaded' mark is dropped so the arriving list is "
               + "actually read")
    }

    function test_a_list_arriving_normally_does_not_disturb_a_loaded_model() {
        if (!requireNoList())
            return
        // The other side of the same handler. With nothing marked loaded there
        // is no stale state to clear, and the screen must not end up claiming
        // a load that did not happen.
        compare(step.modelLoaded, false, "nothing loaded yet")

        ImageWriterSingleton.osListPrepared()

        verify(!step.modelLoaded,
               "a reload that found no data does not report success")
    }

    function test_the_unavailable_signal_does_not_thrash_a_still_empty_list() {
        if (!requireNoList())
            return
        // osListUnavailableChanged fires in both directions. The reload is for
        // the transition back to available; firing while still unavailable
        // must do nothing, or every failed fetch would trigger another reload
        // attempt on a list that is still not there.
        step.modelLoaded = true

        verify(step.osListUnavailable, "still nothing available")
        ImageWriterSingleton.osListUnavailableChanged()

        verify(step.modelLoaded,
               "no reload is forced while the list is still unavailable")
    }

    // -- Moving on ---------------------------------------------------------

    function test_the_keyboard_shortcut_advances_the_same_way_next_does() {
        // The list is given root.next as its nextFunction so that Enter on a
        // row advances the wizard. It has to reach the same signal the Next
        // button does, or Enter and the button would part company.
        const spy = nextSpy.createObject(testCase, { target: step })
        verify(spy !== null)

        step.next()

        compare(spy.count, 1,
                "next() raises nextClicked, which is what the button raises")
        spy.destroy()
    }

    function test_the_list_is_wired_to_that_shortcut() {
        compare(step.hwlist.nextFunction, step.next,
                "so Enter on a row goes through the same route")
        verify(step.hwlist.keyboardAutoAdvance,
               "and Enter is meant to advance at all")
    }

    Component {
        id: nextSpy
        SignalSpy { signalName: "nextClicked" }
    }

    // -- Selecting, once there is something to select ----------------------

    function test_selecting_and_unselecting_tracks_whether_next_is_offered() {
        // currentIndex is what the list reports and hasDeviceSelected is what
        // the Next button reads, so they have to follow each other. Driven
        // directly rather than by clicking, so it holds whether or not this
        // run has any rows to click on.
        step.hwlist.currentIndex = 0
        verify(step.hasDeviceSelected,
               "a selected row means a device is selected")

        step.hwlist.currentIndex = -1
        verify(!step.hasDeviceSelected,
               "and losing the selection means it is not")
    }


    // ── Choosing a board with the mouse ───────────────────────────────
    //
    // The delegate's own click and double-click handlers were uncovered.
    // They are how nearly everyone picks their board, and they are separate
    // from the keyboard path already covered: losing them leaves a list that
    // can be tabbed through and not clicked.

    function rowAt(list, index) {
        list.positionViewAtIndex(index, ListView.Beginning)
        waitForRendering(testCase)
        var row = list.itemAtIndex(index)
        verify(row, "the row is instantiated")
        return row
    }

    function listWithBoards() {
        if (!requireList())
            return null
        var list = step.hwlist
        verify(list, "found the device list")
        if (list.count === 0) {
            skip("the list is there but empty in this run")
            return null
        }
        return list
    }

    function test_clicking_a_board_chooses_it() {
        var list = listWithBoards()
        if (!list)
            return

        mouseClick(rowAt(list, 0))
        waitForRendering(testCase)

        verify(step.hasDeviceSelected, "a board was chosen")
        verify(String(fakeContainer.selectedDeviceName).length > 0,
               "and named to the wizard; got "
               + fakeContainer.selectedDeviceName)
        compare(list.currentIndex, 0, "with the row it came from highlighted")
    }

    function test_clicking_a_board_does_not_move_on_by_itself() {
        // One click chooses; it does not leave the screen. Someone comparing
        // two boards clicks between them, and a list that advanced on the
        // first click would take the choice away from them.
        var list = listWithBoards()
        if (!list)
            return
        const spy = nextSpy.createObject(testCase, { target: step })
        verify(spy !== null)

        mouseClick(rowAt(list, 0))
        waitForRendering(testCase)
        wait(200)

        compare(spy.count, 0, "still on the board chooser")
        spy.destroy()
    }

    function test_double_clicking_a_board_chooses_it_and_moves_on() {
        // The shortcut for someone who knows which board they have.
        var list = listWithBoards()
        if (!list)
            return
        const spy = nextSpy.createObject(testCase, { target: step })
        verify(spy !== null)

        // mouseDoubleClickSequence, not mouseDoubleClick: TestCase has no
        // such function, and the case only reaches this line in a run where
        // the device list was fetched -- so a single-file run skips it and
        // says nothing.
        mouseDoubleClickSequence(rowAt(list, 0))
        waitForRendering(testCase)

        verify(step.hasDeviceSelected, "the board was chosen")
        tryVerify(function () { return spy.count === 1 }, 3000,
                  "and the wizard was asked to move on")
        spy.destroy()
    }

    // ── Trying again ──────────────────────────────────────────────────
    //
    // Retry is the only thing on the offline screen a user can do. If it did
    // nothing, somebody whose connection came back would have no way to find
    // that out short of restarting the application -- which is the one
    // instruction nobody should have to be given.
    //
    // Driven against a repository on disk, so the fetch is the real one and
    // what it answers is under the test's control. The URL never changes;
    // only what is behind it, which is what a connection coming back looks
    // like from here.

    readonly property var aDeviceList: ({
        "imager": {
            "devices": [{
                "name": "Test Pi 5",
                "description": "A board that turned up when the fetch worked",
                "tags": ["test-board"],
                "capabilities": [],
                "icon": "",
                "matching_type": "exclusive",
                "architecture": "arm64",
                "default": true
            }]
        },
        "os_list": [{
            "name": "Test OS",
            "description": "Something to write",
            "url": "https://example.invalid/test.img.xz",
            "icon": "",
            "release_date": "2026-01-01",
            "extract_size": 1048576,
            "image_download_size": 524288,
            "extract_sha256": "ff66"
        }]
    })

    function test_retry_fetches_the_list_again_and_the_screen_comes_back() {
        // The offline configuration only, like the other cases about this
        // screen. The board chooser is what decides whether the offline
        // placeholder is shown, and once it has boards in it there is no
        // supported way to empty it: HWListModel::reload() gives up early on
        // a document with no "imager" section and keeps the rows it had. So
        // in a run where an earlier file fetched a device list, the offline
        // screen cannot be reached at all, and this skips with the rest.
        if (!requireNoList())
            return

        const previousRepo = ImageWriterSingleton.osListUrl()

        // A repository that answers with nothing, so the fetch is real and
        // the screen stays in the state this file is here to cover.
        const name = "device_retry_repo.json"
        const url = TestFiles.write(name, JSON.stringify({}))
        verify(url !== "", "wrote the repository")
        ImageWriterSingleton.refreshOsListFrom(url)
        tryVerify(function () {
            return ImageWriterSingleton.isOsListUnavailable
        }, 10000, "the list is unavailable, as it is with no connection")

        const step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        waitForRendering(step)

        const retry = findChild(step, "deviceListRetryButton")
        verify(retry, "the offline screen offers a way to try again")
        tryVerify(function () { return retry.visible && retry.height > 0 },
                  3000, "and it is on screen to be pressed")

        // The connection comes back: the same URL, answering properly.
        TestFiles.write(name, JSON.stringify(testCase.aDeviceList))

        mouseClick(retry)

        tryVerify(function () {
            return !ImageWriterSingleton.isOsListUnavailable
        }, 10000, "pressing Retry fetched the list again")

        // And the screen notices, rather than leaving the user looking at a
        // warning about a connection that is now working.
        const placeholder = findChild(step, "deviceListOfflinePlaceholder")
        verify(placeholder, "found the offline warning")
        tryVerify(function () { return !placeholder.visible }, 5000,
                  "the offline warning went away")

        step.destroy()

        // Back to no list, or the offline cases after this one skip and the
        // state this file exists to cover goes untested.
        //
        // Two steps, because clearing the document does not empty the board
        // chooser, for the reason at the top of this case. A repository that
        // names an empty device list is fetched first -- an empty array is
        // something reload() will apply -- and the document is only emptied
        // afterwards.
        TestFiles.write(name, JSON.stringify({
            "imager": { "devices": [] },
            "os_list": [{
                "name": "Placeholder",
                "description": "Only here to carry an empty device list",
                "url": "https://example.invalid/placeholder.img.xz",
                "icon": "",
                "release_date": "2026-01-01",
                "extract_size": 1048576,
                "image_download_size": 524288,
                "extract_sha256": "0011"
            }]
        }))
        ImageWriterSingleton.refreshOsListFrom(url)
        tryVerify(function () {
            return !ImageWriterSingleton.isOsListUnavailable
        }, 10000, "the emptying repository was fetched")
        ImageWriterSingleton.getHWList().reload()

        TestFiles.write(name, JSON.stringify({}))
        ImageWriterSingleton.refreshOsListFrom(url)
        tryVerify(function () {
            return ImageWriterSingleton.isOsListUnavailable
        }, 10000, "the empty state is back for the cases that need it")

        ImageWriterSingleton.setCustomRepo(previousRepo)
    }
}
