/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * DeviceSelectionStep: the first screen with something on it that can fail.
 *
 * The device list comes off the network, so this screen is the one a user
 * meets when they open the application with no connection: an empty list, a
 * warning, and a Retry button. It was at 30%, and everything about that state
 * was untested.
 *
 * Two things about it matter more than the rest.
 *
 * The first is not trapping the user. With no device list there is nothing to
 * select, and a Next button gated on a selection would leave them unable to
 * move at all -- yet a custom image needs no device chosen. So Next has to be
 * enabled precisely when the list is empty because the fetch failed.
 *
 * The second is coming back from it. If the list is fetched successfully after
 * the screen has already given up, the screen has to notice and reload. The
 * handler for that turns on a specific case -- the model was marked loaded,
 * but has no rows in it, because it was "loaded" while offline -- and forcing
 * the reload is the whole point: without it the arriving list is ignored and
 * the user sits in front of an empty screen with a working connection.
 *
 * Which of those two states the step comes up in is not this file's to choose.
 * The device list lives on the ImageWriter singleton, one instance shared by
 * every test file in the process, and it holds whatever the files that ran
 * earlier left behind: run on its own this file finds no list at all, run as
 * part of the suite it finds one another file has fetched. The singleton
 * offers no way to empty it again that does not start a fresh network fetch.
 *
 * So each case says which state it needs and skips in the other, and both
 * states are covered rather than only the one that happens to turn up. The
 * cases that depend on neither -- the wiring between the list, the selection
 * and the Next button -- always run.
 *
 * The signals the screen listens for are emitted from here directly, which
 * the metaobject allows. Retry calls ImageWriterSingleton.beginOSListFetch(),
 * which starts a real network fetch, so it is not clicked: what is checked is
 * that it is there, labelled and described, since a user with no list and no
 * visible way to try again has nothing to do but restart the application.
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
}
