/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * WizardContainer: which steps the user is allowed to reach, and which of
 * them the sidebar shows as done.
 *
 * Two things hang off this. The sidebar only lets you jump to a step that
 * has been marked permissible, so getting it wrong either strands someone
 * on a step they cannot leave or lets them skip past configuration they
 * have not done. And choosing a different device has to invalidate
 * everything chosen after it -- an OS and a storage device picked for the
 * previous board must not still be reachable, or the write goes ahead with
 * a mismatched pair.
 *
 * The customisation substeps are a separate hazard: the list is built at
 * runtime from what the board supports, so the same index means different
 * steps on different hardware.
 *
 * Note for anyone running this file on its own with -input: nine of the
 * cases below need a populated OS list and there is nothing in this file
 * that populates one, so they fail in isolation and pass in the full run,
 * where another file has already fetched or fed it. That is the harness,
 * not the wizard.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "WizardNavigation"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Component {
        id: containerComponent
        WizardContainer {}
    }

    // Built once: instantiating the container builds every step inside it.
    // createObject rather than createTemporaryObject, which would destroy it
    // at the end of the first test function.
    property var wiz: null

    function initTestCase() {
        wiz = containerComponent.createObject(testCase)
        verify(wiz, "the wizard container was created")
        // main.qml supplies this in the application; the steps parent their
        // dialogs onto it, and without it those assignments fail and the
        // dialogs end up unparented -- which nothing here would notice.
        wiz.overlayRootRef = testCase
    }

    function cleanupTestCase() {
        if (wiz) {
            wiz.destroy()
            wiz = null
        }
    }

    function init() {
        // Each case starts from the state a fresh run has.
        wiz.permissibleStepsBitmap = 1
        wiz.customizationSupported = true
        wiz.secureBootAvailable = false
        wiz.piConnectAvailable = false
        wiz.ccRpiAvailable = false
        wiz.hostnameConfigured = false
        wiz.localeConfigured = false
        wiz.userConfigured = false
        wiz.wifiConfigured = false
        wiz.sshEnabled = false
        wiz.secureBootEnabled = false
        wiz.piConnectEnabled = false
        wiz.ifAndFeaturesAvailable = false
        wiz.ifI2cEnabled = false
        wiz.ifSpiEnabled = false
        wiz.if1WireEnabled = false
        wiz.ifSerial = ""
        wiz.featUsbGadgetEnabled = false
        wiz.selectedOsName = ""
        wiz.selectedStorageName = ""
    }

    // -- Which steps can be jumped to --------------------------------------

    function test_only_device_selection_is_reachable_at_the_start() {
        // Nothing has been chosen, so nothing after the first step should be
        // reachable from the sidebar.
        verify(wiz.isStepPermissible(wiz.stepDeviceSelection))
        verify(!wiz.isStepPermissible(wiz.stepOSSelection))
        verify(!wiz.isStepPermissible(wiz.stepStorageSelection))
        verify(!wiz.isStepPermissible(wiz.stepWriting))
    }

    function test_marking_a_step_makes_it_reachable() {
        verify(!wiz.isStepPermissible(wiz.stepStorageSelection))
        wiz.markStepPermissible(wiz.stepStorageSelection)
        verify(wiz.isStepPermissible(wiz.stepStorageSelection))
    }

    function test_marking_one_step_does_not_mark_its_neighbours() {
        wiz.markStepPermissible(wiz.stepUserCustomization)
        verify(wiz.isStepPermissible(wiz.stepUserCustomization))
        verify(!wiz.isStepPermissible(wiz.stepLocaleCustomization))
        verify(!wiz.isStepPermissible(wiz.stepWifiCustomization))
    }

    function test_marking_a_step_twice_is_harmless() {
        wiz.markStepPermissible(wiz.stepOSSelection)
        wiz.markStepPermissible(wiz.stepOSSelection)
        verify(wiz.isStepPermissible(wiz.stepOSSelection))
    }

    function test_every_step_can_be_marked_and_read_back() {
        // The bitmap is 1 << index, so an index past 31 would silently wrap
        // onto another step. There are 13, but this pins that the last one
        // is genuinely distinguishable from the first.
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++) {
            wiz.permissibleStepsBitmap = 0
            wiz.markStepPermissible(i)
            verify(wiz.isStepPermissible(i), "step " + i + " reads back")
            for (let j = wiz.stepDeviceSelection; j <= wiz.stepDone; j++) {
                if (j !== i)
                    verify(!wiz.isStepPermissible(j),
                           "step " + i + " did not also mark " + j)
            }
        }
    }

    // -- Choosing a different device invalidates what followed -------------

    function test_invalidating_from_a_step_clears_it_and_everything_after() {
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)

        wiz.invalidateStepsFrom(wiz.stepStorageSelection)

        verify(wiz.isStepPermissible(wiz.stepDeviceSelection), "before is kept")
        verify(wiz.isStepPermissible(wiz.stepOSSelection), "before is kept")
        verify(!wiz.isStepPermissible(wiz.stepStorageSelection),
               "the step itself goes")
        verify(!wiz.isStepPermissible(wiz.stepUserCustomization), "and after")
        verify(!wiz.isStepPermissible(wiz.stepWriting))
    }

    function test_invalidating_from_the_first_step_clears_everything() {
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)

        wiz.invalidateStepsFrom(wiz.stepDeviceSelection)
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            verify(!wiz.isStepPermissible(i), "step " + i + " cleared")
    }

    function test_invalidating_past_the_end_keeps_everything() {
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)

        wiz.invalidateStepsFrom(wiz.stepDone + 1)
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            verify(wiz.isStepPermissible(i), "step " + i + " kept")
    }

    function test_changing_device_invalidates_the_os_and_everything_after() {
        // The case that matters: an OS and a card chosen for the previous
        // board must not stay reachable once the board changes.
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedOsName = "Raspberry Pi OS"
        wiz.selectedStorageName = "Generic SD"
        wiz.hostnameConfigured = true

        wiz.invalidateDeviceDependentSteps()

        verify(wiz.isStepPermissible(wiz.stepDeviceSelection),
               "the device step itself stays reachable")
        verify(!wiz.isStepPermissible(wiz.stepOSSelection))
        verify(!wiz.isStepPermissible(wiz.stepStorageSelection))
        compare(wiz.selectedOsName, "", "and the choices are forgotten")
        compare(wiz.selectedStorageName, "")
        compare(wiz.hostnameConfigured, false)
    }

    // -- When a change counts as a change ----------------------------------
    //
    // The case above calls invalidateDeviceDependentSteps() directly, so it
    // shows what invalidation does but not when it happens. That decision
    // lives in the property-change handlers, and it has to distinguish three
    // things: the first selection, a real change, and re-selecting what is
    // already chosen.
    //
    // The third is the one with teeth. Without the inequality check, clicking
    // the board you already had selected would discard the OS, the card and
    // every customisation flag -- work the user had done, thrown away by an
    // action that changed nothing.

    function selectDeviceFresh(name) {
        // As a fresh run arrives at this step: nothing chosen before.
        //
        // Clearing first matters. These cases share one container, so the
        // name may already be the one being selected -- and assigning a
        // property its current value emits no change, so the handler under
        // test would never run and the case would pass having exercised
        // nothing. previousDeviceName is emptied before the clear so that
        // the clear itself is not treated as a change either.
        wiz.previousDeviceName = ""
        if (wiz.selectedDeviceName === name)
            wiz.selectedDeviceName = ""
        wiz.previousDeviceName = ""
        wiz.selectedDeviceName = name
    }

    function test_the_first_device_choice_invalidates_nothing() {
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedOsName = "Raspberry Pi OS"
        wiz.previousOsName = "Raspberry Pi OS"

        selectDeviceFresh("Raspberry Pi 5")

        verify(wiz.isStepPermissible(wiz.stepOSSelection),
               "nothing was thrown away")
        compare(wiz.selectedOsName, "Raspberry Pi OS")
        compare(wiz.previousDeviceName, "Raspberry Pi 5",
                "and the choice is remembered for next time")
    }

    function test_choosing_a_different_device_invalidates_what_followed() {
        selectDeviceFresh("Raspberry Pi 5")
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedOsName = "Raspberry Pi OS"
        wiz.selectedStorageName = "Generic SD"
        wiz.hostnameConfigured = true

        wiz.selectedDeviceName = "Raspberry Pi 4"

        verify(!wiz.isStepPermissible(wiz.stepOSSelection),
               "the OS chosen for the other board is no longer reachable")
        compare(wiz.selectedOsName, "")
        compare(wiz.selectedStorageName, "")
        compare(wiz.hostnameConfigured, false)
    }

    function test_arriving_at_the_device_already_recorded_keeps_everything() {
        // The handler's second condition: it fires only when the name it
        // last saw differs from the one now set.
        //
        // Re-clicking the selected board cannot show this. QML emits no
        // change for an assignment of the value a property already holds, so
        // the handler never runs and the condition is never reached -- a test
        // written that way passes whatever the condition says, which is how
        // this one started out.
        //
        // Reaching it needs the two to agree at the moment the handler runs,
        // so the remembered name is set to the incoming one first. That is
        // the shape resetWizard() leaves behind, since it clears both.
        selectDeviceFresh("Raspberry Pi 5")
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedOsName = "Raspberry Pi OS"
        wiz.selectedStorageName = "Generic SD"
        wiz.hostnameConfigured = true

        wiz.previousDeviceName = "Raspberry Pi 4"
        wiz.selectedDeviceName = "Raspberry Pi 4"

        verify(wiz.isStepPermissible(wiz.stepOSSelection))
        compare(wiz.selectedOsName, "Raspberry Pi OS", "the OS survived")
        compare(wiz.selectedStorageName, "Generic SD")
        compare(wiz.hostnameConfigured, true)
    }

    function test_choosing_a_different_os_invalidates_the_card_but_not_the_board() {
        // An OS change invalidates from storage onward: the card was chosen
        // knowing the image size, and the customisation depends on the OS.
        // The board is upstream of the choice and stays.
        wiz.previousOsName = "Raspberry Pi OS"
        wiz.selectedOsName = "Raspberry Pi OS"
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedStorageName = "Generic SD"
        wiz.hostnameConfigured = true

        wiz.selectedOsName = "Ubuntu Server"

        verify(wiz.isStepPermissible(wiz.stepDeviceSelection),
               "the board is untouched")
        verify(wiz.isStepPermissible(wiz.stepOSSelection),
               "and so is the step that was just used")
        verify(!wiz.isStepPermissible(wiz.stepStorageSelection))
        compare(wiz.selectedStorageName, "")
        compare(wiz.hostnameConfigured, false)
    }

    function test_arriving_at_the_os_already_recorded_keeps_everything() {
        // As above, for the OS handler: the two are made to agree so the
        // condition is actually evaluated rather than skipped.
        wiz.previousOsName = "Raspberry Pi OS"
        wiz.selectedOsName = "Ubuntu Server"
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedStorageName = "Generic SD"
        wiz.hostnameConfigured = true

        wiz.previousOsName = "Raspberry Pi OS"
        wiz.selectedOsName = "Raspberry Pi OS"

        compare(wiz.selectedStorageName, "Generic SD")
        compare(wiz.hostnameConfigured, true)
    }

    // -- Which step comes next ---------------------------------------------
    //
    // nextStep() does more than add one. It skips the optional steps the
    // selected OS cannot use, and skips the whole customisation section when
    // the OS supports none of it. Getting that wrong strands someone on a
    // step for a feature their board does not have, or walks them past one
    // they needed.
    //
    // The indices are deliberately spelled out rather than derived: the
    // point is which step the user lands on, and computing the expectation
    // the same way the code does would assert nothing.

    function startAt(step) {
        wiz.customizationSupported = true
        wiz.secureBootAvailable = true
        wiz.piConnectAvailable = true
        wiz.ccRpiAvailable = true
        wiz.ifAndFeaturesAvailable = true
        wiz.writeAnotherMode = false
        wiz.currentStep = step
    }

    function test_the_ordinary_path_goes_one_step_at_a_time() {
        startAt(wiz.stepDeviceSelection)
        wiz.nextStep()
        compare(wiz.currentStep, wiz.stepOSSelection)
        wiz.nextStep()
        compare(wiz.currentStep, wiz.stepStorageSelection)
        wiz.nextStep()
        compare(wiz.currentStep, wiz.stepHostnameCustomization,
                "and into customisation")
    }

    function test_an_os_without_customisation_skips_the_whole_section() {
        // Otherwise the user is walked through hostname, locale, user, wifi
        // and the rest for an image that cannot carry any of it.
        startAt(wiz.stepStorageSelection)
        wiz.customizationSupported = false

        wiz.nextStep()

        compare(wiz.currentStep, wiz.stepWriting)
    }

    function test_secure_boot_is_skipped_when_the_os_cannot_use_it() {
        startAt(wiz.stepRemoteAccess)
        wiz.secureBootAvailable = false

        wiz.nextStep()

        compare(wiz.currentStep, wiz.stepPiConnectCustomization,
                "straight past secure boot")
    }

    function test_pi_connect_is_skipped_when_the_os_cannot_use_it() {
        startAt(wiz.stepSecureBootCustomization)
        wiz.piConnectAvailable = false

        wiz.nextStep()

        compare(wiz.currentStep, wiz.stepIfAndFeatures)
    }

    function test_interfaces_are_skipped_when_the_board_offers_none_data() {
        // Either reason is enough on its own: the OS not supporting cc-rpi,
        // or the board advertising no interfaces to enable.
        return [
            { tag: "no cc-rpi",     ccRpi: false, ifAvailable: true },
            { tag: "no interfaces", ccRpi: true,  ifAvailable: false },
            { tag: "neither",       ccRpi: false, ifAvailable: false }
        ]
    }

    function test_interfaces_are_skipped_when_the_board_offers_none(data) {
        startAt(wiz.stepPiConnectCustomization)
        wiz.ccRpiAvailable = data.ccRpi
        wiz.ifAndFeaturesAvailable = data.ifAvailable

        wiz.nextStep()

        compare(wiz.currentStep, wiz.stepWriting, data.tag)
    }

    function test_the_last_step_does_not_advance_past_the_end() {
        startAt(wiz.stepDone)

        wiz.nextStep()

        compare(wiz.currentStep, wiz.stepDone)
    }

    // -- Writing a second card ---------------------------------------------

    function test_write_another_goes_straight_from_the_card_to_writing() {
        // The point of the mode: the user has already answered everything,
        // and is only choosing where the next copy goes.
        startAt(wiz.stepStorageSelection)
        wiz.writeAnotherMode = true

        wiz.nextStep()

        compare(wiz.currentStep, wiz.stepWriting)
    }

    function test_write_another_is_a_one_time_shortcut() {
        // The flag is cleared on use. Left set, the next pass through the
        // wizard would skip customisation the user had gone back to change.
        startAt(wiz.stepStorageSelection)
        wiz.writeAnotherMode = true
        wiz.nextStep()
        compare(wiz.currentStep, wiz.stepWriting)

        verify(!wiz.writeAnotherMode, "the shortcut was spent")

        startAt(wiz.stepStorageSelection)
        wiz.nextStep()
        compare(wiz.currentStep, wiz.stepHostnameCustomization,
                "so the next pass walks through customisation again")
    }

    // -- What the completion screen is told --------------------------------

    function test_the_summary_is_captured_on_the_way_into_writing() {
        // Taken here because the write itself clears the Connect token and
        // other session state; the Done screen would otherwise report a
        // configuration that no longer exists.
        startAt(wiz.stepRemoteAccess)
        wiz.hostnameConfigured = true
        wiz.userConfigured = true
        wiz.sshEnabled = true
        wiz.wifiConfigured = false
        wiz.secureBootAvailable = false
        wiz.piConnectAvailable = false
        wiz.ccRpiAvailable = false

        wiz.nextStep()
        compare(wiz.currentStep, wiz.stepWriting)

        var snap = wiz.completionSnapshot
        verify(snap, "a snapshot was taken")
        compare(snap.hostnameConfigured, true)
        compare(snap.userConfigured, true)
        compare(snap.sshEnabled, true)
        compare(snap.wifiConfigured, false)
    }

    // -- Going back --------------------------------------------------------
    //
    // previousStep() has to skip the same optional steps forward navigation
    // does, or Back lands on a screen for a feature the selected OS cannot
    // use -- and from writing with no customisation at all it has to clear
    // the whole section in one move rather than walking backwards through
    // steps that were never shown.
    //
    // The offline guards are not covered here. Both read
    // hasNetworkConnectivity, which is derived from whether the OS list is
    // empty, and the list lives on the singleton shared by every case in
    // this binary -- so whether a test is "offline" depends on what ran
    // before it. Asserting the precondition instead means these cases fail
    // loudly if that ever changes, rather than quietly testing the other
    // branch.

    function test_going_back_skips_the_same_steps_going_forward_did_data() {
        return [
            { tag: "interfaces: no cc-rpi",  from: "ifAndFeatures",
              flag: "ccRpiAvailable",       to: "piConnect" },
            { tag: "interfaces: none there", from: "ifAndFeatures",
              flag: "ifAndFeaturesAvailable", to: "piConnect" },
            { tag: "pi connect",             from: "piConnect",
              flag: "piConnectAvailable",   to: "secureBoot" },
            { tag: "secure boot",            from: "secureBoot",
              flag: "secureBootAvailable",  to: "remoteAccess" }
        ]
    }

    function stepFor(name) {
        switch (name) {
        case "ifAndFeatures": return wiz.stepIfAndFeatures
        case "piConnect":     return wiz.stepPiConnectCustomization
        case "secureBoot":    return wiz.stepSecureBootCustomization
        case "remoteAccess":  return wiz.stepRemoteAccess
        }
        return -99
    }

    function test_going_back_skips_the_same_steps_going_forward_did(data) {
        verify(wiz.hasNetworkConnectivity,
               "these cases assume the OS list is populated")
        startAt(stepFor(data.from) + 1)
        wiz[data.flag] = false

        wiz.previousStep()

        compare(wiz.currentStep, stepFor(data.to), data.tag)
    }

    function test_going_back_from_writing_without_customisation_reaches_the_card() {
        // One move, not eleven: none of the customisation steps were shown
        // on the way in, so walking back through them would show screens the
        // user has never seen and cannot use.
        verify(wiz.hasNetworkConnectivity)
        startAt(wiz.stepWriting)
        wiz.customizationSupported = false

        wiz.previousStep()

        compare(wiz.currentStep, wiz.stepStorageSelection)
    }

    function test_going_back_from_the_first_step_does_nothing() {
        startAt(wiz.stepDeviceSelection)

        wiz.previousStep()

        compare(wiz.currentStep, wiz.stepDeviceSelection)
    }

    // -- Jumping from the sidebar ------------------------------------------

    function test_jumping_to_a_step_goes_there() {
        verify(wiz.hasNetworkConnectivity)
        startAt(wiz.stepDeviceSelection)

        wiz.jumpToStep(wiz.stepStorageSelection)

        compare(wiz.currentStep, wiz.stepStorageSelection)
    }

    function test_jumping_outside_the_wizard_does_nothing_data() {
        return [
            { tag: "before the first", index: -1 },
            { tag: "past the last",    index: 13 },
            { tag: "far past",         index: 999 }
        ]
    }

    function test_jumping_outside_the_wizard_does_nothing(data) {
        verify(wiz.hasNetworkConnectivity)
        startAt(wiz.stepStorageSelection)

        wiz.jumpToStep(data.index)

        compare(wiz.currentStep, wiz.stepStorageSelection, data.tag)
    }

    // -- Starting over, and writing another copy ---------------------------
    //
    // Two resets with deliberately different reach. Starting over clears
    // everything, including the writer's own source and destination -- left
    // set, a later write has a target nobody chose. Writing another copy
    // keeps the board, the OS and the customisation, because the user
    // answered all of that already, and clears only what must not be reused:
    // the card, and the Connect flag whose token has already been discarded.

    function test_starting_over_forgets_every_choice() {
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedDeviceName = "Raspberry Pi 5"
        wiz.selectedOsName = "Raspberry Pi OS"
        wiz.selectedStorageName = "Generic SD"
        wiz.hostnameConfigured = true
        wiz.userConfigured = true
        wiz.sshEnabled = true
        wiz.piConnectEnabled = true

        wiz.resetWizard()

        compare(wiz.selectedDeviceName, "")
        compare(wiz.selectedOsName, "")
        compare(wiz.selectedStorageName, "")
        compare(wiz.hostnameConfigured, false)
        compare(wiz.userConfigured, false)
        compare(wiz.sshEnabled, false)
        compare(wiz.piConnectEnabled, false)
        verify(!wiz.isStepPermissible(wiz.stepOSSelection),
               "and nothing past the first step is reachable again")
    }

    function test_starting_over_leaves_the_writer_with_no_target() {
        // The wizard's own fields are not the whole story: ImageWriter holds
        // the source and destination it was given, and a stale destination is
        // a drive the user did not choose for the next write.
        ImageWriterSingleton.setSrc("file:///tmp/whatever.img")
        ImageWriterSingleton.setDst("/dev/null", 1024 * 1024)
        verify(ImageWriterSingleton.readyToWrite(), "the writer had a target")

        wiz.resetWizard()

        verify(!ImageWriterSingleton.readyToWrite(),
               "and does not after starting over")
    }

    function test_starting_over_forgets_what_was_previously_selected() {
        // previousDeviceName is what decides whether the next selection
        // counts as a change; left set, the first choice of the new run
        // would invalidate steps that were never configured.
        //
        // This documents the outcome rather than guarding the line that
        // produces it. resetWizard() assigns these explicitly, but it also
        // clears selectedDeviceName, and the change handler then sets the
        // remembered name from it -- so removing the explicit assignments
        // leaves the behaviour intact and this case green. Both routes would
        // have to go for it to fail, which is worth knowing before anyone
        // reads a passing run as proof that either one is load-bearing.
        selectDeviceFresh("Raspberry Pi 5")
        compare(wiz.previousDeviceName, "Raspberry Pi 5")

        wiz.resetWizard()

        compare(wiz.previousDeviceName, "")
        compare(wiz.previousOsName, "")
    }

    function test_writing_another_copy_keeps_the_answers_and_drops_the_card() {
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedDeviceName = "Raspberry Pi 5"
        wiz.selectedOsName = "Raspberry Pi OS"
        wiz.selectedStorageName = "The first card"
        wiz.hostnameConfigured = true
        wiz.userConfigured = true

        wiz.resetToWriteStep()

        compare(wiz.selectedStorageName, "", "the card has to be chosen again")
        compare(wiz.selectedDeviceName, "Raspberry Pi 5", "the board is kept")
        compare(wiz.selectedOsName, "Raspberry Pi OS", "so is the OS")
        compare(wiz.hostnameConfigured, true, "and the customisation")
        compare(wiz.userConfigured, true)
        compare(wiz.currentStep, wiz.stepStorageSelection,
                "and it opens on the card chooser")
    }

    function test_writing_another_copy_does_not_reuse_the_first_card() {
        // Same point at the writer level: the destination must be cleared, or
        // pressing on would write a second image over the card just finished.
        ImageWriterSingleton.setSrc("file:///tmp/whatever.img")
        ImageWriterSingleton.setDst("/dev/null", 1024 * 1024)
        verify(ImageWriterSingleton.readyToWrite())

        wiz.resetToWriteStep()

        verify(!ImageWriterSingleton.readyToWrite(),
               "no destination is carried over")
    }

    function test_writing_another_copy_drops_the_connect_enrolment() {
        // The Connect token is session-only and is discarded when the write
        // finishes. Leaving the flag set would tell the generator to enrol
        // the second board with a token that no longer exists.
        wiz.piConnectEnabled = true
        wiz.customizationSettings.piConnectEnabled = true

        wiz.resetToWriteStep()

        compare(wiz.piConnectEnabled, false)
        verify(wiz.customizationSettings.piConnectEnabled === undefined,
               "and it is removed from what the generator is given")
    }

    function test_writing_another_copy_arms_the_shortcut() {
        wiz.resetToWriteStep()

        verify(wiz.writeAnotherMode,
               "so the card chooser leads straight to writing")
    }

    // -- What a deep link is allowed to change -----------------------------
    //
    // A link handed to the application can ask to replace the source of
    // every image on offer. That is not something to do quietly, so it is
    // put to the user -- and the asking has a guard on it worth keeping:
    // once the dialog is up for one URL, a second link naming a different
    // one is ignored rather than swapped in underneath. The comment in the
    // source calls that a race condition attack, and it is exactly one: open
    // a dialog for something harmless, then change the answer as the user
    // reaches for Switch.
    //
    // Driven through handleIncomingUrl() rather than by calling the handler,
    // because the handler lives on a Connections block and is not a method
    // of the container -- and going in at the front door exercises the
    // validation, the signal and the QML response together.

    function repoDialog() {
        var d = findChild(wiz, "repositoryUrlDialog")
        verify(d, "found the repository dialog")
        return d
    }

    function sendRepoLink(url) {
        ImageWriterSingleton.handleIncomingUrl("rpi-imager://open?repo=" + url)
    }

    function closeRepoDialog() {
        repoDialog().close()
        tryVerify(function () { return !repoDialog().visible }, 3000)
    }

    function test_a_repository_link_is_put_to_the_user() {
        sendRepoLink("https://example.invalid/os_list.json")

        tryVerify(function () { return repoDialog().opened }, 3000,
                  "the dialog was raised")
        compare(repoDialog().repoUrl, "https://example.invalid/os_list.json")

        closeRepoDialog()
    }

    function test_a_repository_link_cannot_be_accepted_at_once() {
        // Same reasoning as the erase confirmation: someone already pressing
        // Return must not confirm a source change they never read.
        sendRepoLink("https://example.invalid/os_list.json")
        tryVerify(function () { return repoDialog().opened }, 3000)

        verify(!repoDialog().allowAccept)

        closeRepoDialog()
    }

    function test_a_malformed_repository_link_asks_nothing() {
        // Refused in C++ before any signal is emitted, so the dialog never
        // appears -- the user is not asked about a URL that could not be
        // used anyway.
        sendRepoLink("not-a-url")
        sendRepoLink("file:///etc/passwd.json")

        wait(300)
        verify(!repoDialog().opened)
    }

    function test_a_second_link_cannot_change_the_url_being_asked_about() {
        // The guard. Without it the dialog goes on saying one thing while
        // Switch would apply another.
        sendRepoLink("https://example.invalid/harmless.json")
        tryVerify(function () { return repoDialog().opened }, 3000)

        sendRepoLink("https://elsewhere.invalid/other.json")

        compare(repoDialog().repoUrl, "https://example.invalid/harmless.json",
                "still asking about the first one")

        closeRepoDialog()
    }

    function test_closing_the_dialog_forgets_the_url() {
        // Otherwise the next link arrives to a dialog that already holds a
        // URL, and the guard above would treat it as the one on screen.
        sendRepoLink("https://example.invalid/os_list.json")
        tryVerify(function () { return repoDialog().opened }, 3000)

        repoDialog().close()

        tryVerify(function () { return repoDialog().repoUrl === "" }, 3000)
        verify(!repoDialog().allowAccept)
    }

    // -- The Connect token, when the step that owns it is not loaded --------

    function test_a_token_conflict_is_put_to_the_user() {
        // A second link carrying a different token must not replace the one
        // the image was set up with; the container asks instead.
        var first = "rpuak_abcdefghijkmnpqrstuvwxyz"
        var second = "rpuak_zyxwvutsrqpnmkjihgfedcba"
        ImageWriterSingleton.clearConnectToken()
        ImageWriterSingleton.handleIncomingUrl("rpi-imager://connect?auth_key=" + first)

        ImageWriterSingleton.handleIncomingUrl("rpi-imager://connect?auth_key=" + second)

        var d = findChild(wiz, "tokenConflictDialog")
        verify(d, "found the conflict dialog")
        tryVerify(function () { return d.opened }, 3000,
                  "the user is asked rather than the token being swapped")
        compare(ImageWriterSingleton.getRuntimeConnectToken(), first,
                "and the token in use is untouched")

        d.close()
        tryVerify(function () { return !d.visible }, 3000)
        ImageWriterSingleton.clearConnectToken()
    }

    function test_clearing_the_token_resets_connect_here() {
        // Handled on the container rather than the Pi Connect step, because
        // the token is cleared when a write finishes and that step may not
        // be loaded -- on this path nothing else would reset the flag, and
        // the next image would claim Connect was configured.
        ImageWriterSingleton.handleIncomingUrl(
            "rpi-imager://connect?auth_key=rpuak_abcdefghijkmnpqrstuvwxyz")
        wiz.piConnectEnabled = true
        wiz.customizationSettings.piConnectEnabled = true

        ImageWriterSingleton.clearConnectToken()

        tryVerify(function () { return wiz.piConnectEnabled === false }, 3000)
        verify(wiz.customizationSettings.piConnectEnabled === undefined,
               "and it is gone from what the generator is given")
    }

    // -- The customisation substeps ----------------------------------------

    function test_the_base_substeps_are_always_offered() {
        const labels = wiz.getCustomizationSubstepLabels()
        compare(labels.length, 5)
    }

    function test_a_board_without_customisation_offers_no_substeps() {
        wiz.customizationSupported = false
        compare(wiz.getCustomizationSubstepLabels().length, 0)
    }

    function test_optional_substeps_appear_only_when_the_board_has_them() {
        compare(wiz.getCustomizationSubstepLabels().length, 5)

        wiz.secureBootAvailable = true
        compare(wiz.getCustomizationSubstepLabels().length, 6)

        wiz.piConnectAvailable = true
        compare(wiz.getCustomizationSubstepLabels().length, 7)
    }

    function test_each_substep_reports_its_own_flag() {
        // Index to flag, over the fixed five.
        wiz.userConfigured = true
        verify(!wiz.isCustomizationSubstepConfigured(0), "hostname")
        verify(!wiz.isCustomizationSubstepConfigured(1), "localisation")
        verify(wiz.isCustomizationSubstepConfigured(2), "user")
        verify(!wiz.isCustomizationSubstepConfigured(3), "wi-fi")
        verify(!wiz.isCustomizationSubstepConfigured(4), "remote access")

        wiz.wifiConfigured = true
        verify(wiz.isCustomizationSubstepConfigured(3))
    }

    function test_the_same_index_means_different_steps_on_different_boards() {
        // Index 5 is Secure Boot on a board that has it and Pi Connect on a
        // board that does not, because the list is built from what is
        // available. A tick against the wrong row is a step the user thinks
        // they have done.
        wiz.secureBootEnabled = true
        wiz.piConnectEnabled = false

        wiz.secureBootAvailable = true
        wiz.piConnectAvailable = false
        verify(wiz.isCustomizationSubstepConfigured(5), "secure boot at 5")

        wiz.secureBootAvailable = false
        wiz.piConnectAvailable = true
        verify(!wiz.isCustomizationSubstepConfigured(5),
               "pi connect at 5, and it is not enabled")

        wiz.piConnectEnabled = true
        verify(wiz.isCustomizationSubstepConfigured(5))
    }

    function test_an_index_past_the_end_is_not_configured() {
        verify(!wiz.isCustomizationSubstepConfigured(5),
               "nothing optional is available, so 5 is off the end")
        verify(!wiz.isCustomizationSubstepConfigured(99))
    }

    function test_interfaces_counts_as_configured_when_any_one_is_on() {
        wiz.ccRpiAvailable = true
        wiz.ifAndFeaturesAvailable = true
        const labels = wiz.getCustomizationSubstepLabels()
        const idx = labels.length - 1

        verify(!wiz.isCustomizationSubstepConfigured(idx), "nothing enabled")

        wiz.ifSpiEnabled = true
        verify(wiz.isCustomizationSubstepConfigured(idx), "spi alone counts")

        wiz.ifSpiEnabled = false
        wiz.ifSerial = "Enabled"
        verify(wiz.isCustomizationSubstepConfigured(idx), "a serial mode counts")

        wiz.ifSerial = "Disabled"
        verify(!wiz.isCustomizationSubstepConfigured(idx),
               "but Disabled is not a configuration")

        wiz.ifSerial = ""
        verify(!wiz.isCustomizationSubstepConfigured(idx),
               "and neither is empty")
    }


    // -- When Next is allowed to move the wizard on ------------------------
    //
    // Two of the steps do not let the container advance on their own say-so.
    // Pi Connect holds until the token it was given is one the service will
    // accept, and the writing step holds until the write has finished; in
    // each case the container asks the step first and the step handles the
    // press itself when the answer is no. Those handlers were uncovered.
    //
    // They are the other half of the confirmations tested elsewhere: a step
    // that records an answer is decorative if the container advances
    // regardless of it. The writing one is the worst of them -- the
    // completion screen is what tells a user the card can come out, and
    // reaching it during a write is how a card gets pulled half-written.

    // The stack is an id inside the container rather than an exposed
    // property, so it is found by walking the object tree.
    function findStepStack(item) {
        if (!item)
            return null
        if (item.currentItem !== undefined && item.depth !== undefined)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; i++) {
            const found = findStepStack(kids[i])
            if (found)
                return found
        }
        return null
    }

    function goToStep(step) {
        for (let i = wiz.stepDeviceSelection; i <= wiz.stepDone; i++)
            wiz.markStepPermissible(i)
        wiz.selectedDeviceName = "Raspberry Pi 5"
        wiz.selectedOsName = "Raspberry Pi OS (64-bit)"
        wiz.selectedStorageName = "Generic Mass-Storage 32 GB"
        wiz.jumpToStep(step)
        compare(wiz.currentStep, step, "the wizard is on the step")
        const stack = findStepStack(wiz)
        verify(stack !== null, "the container has a step stack")
        verify(stack.currentItem !== null, "and something on it")
        // Several steps finish setting themselves up on the next turn of the
        // event loop; anything read before that is the half-built version.
        wait(150)
        return stack.currentItem
    }

    // Left behind, a write in progress changes what every later case in this
    // file sees on the container.
    function leaveTheWriterIdle() {
        ImageWriterSingleton.onCancelled()
        wait(50)
    }

    function test_the_writing_step_does_not_advance_while_it_is_writing() {
        const step = goToStep(wiz.stepWriting)
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return step.isWriting }, 3000,
                  "a write is running")
        verify(!step.isComplete, "and it is not finished")

        step.nextClicked()

        compare(wiz.currentStep, wiz.stepWriting,
                "the user is still watching the write")
        leaveTheWriterIdle()
    }

    function test_a_cancelled_write_does_not_count_as_a_finished_one() {
        // Cancelling leaves the card partly written. The step stays put and
        // says what happened rather than handing the user a completion
        // screen for a card that is not usable.
        const step = goToStep(wiz.stepWriting)
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return step.isWriting }, 3000)
        ImageWriterSingleton.onCancelled()
        tryVerify(function () { return !step.isWriting }, 3000)
        verify(!step.isComplete)

        step.nextClicked()

        compare(wiz.currentStep, wiz.stepWriting)
        leaveTheWriterIdle()
    }

    function test_a_finished_write_moves_on_by_itself() {
        // Not what a first draft asserted. Next is not what leaves this
        // screen on success: a finished write takes the user to the
        // completion screen on its own, so a case that pressed Next and
        // then checked it had arrived was reading a step the wizard had
        // already reached. Measured -- making the container refuse every
        // press from this screen left that case passing.
        //
        // So the container's condition is pinned by the two refusals above,
        // both of which fail when it advances regardless. What this pins is
        // that a user who has just watched a write does not have to press
        // anything to be told the card is ready.
        //
        // Through finalising first: a success arriving while the writer is
        // in a cancelled state is refused as a late signal, and the cases
        // above leave it cancelled behind them.
        const step = goToStep(wiz.stepWriting)
        compare(wiz.currentStep, wiz.stepWriting, "starting on the write")
        ImageWriterSingleton.onFinalizing()
        tryVerify(function () { return step.isWriting }, 3000)

        ImageWriterSingleton.onSuccess()

        tryVerify(function () { return wiz.currentStep === wiz.stepDone }, 3000,
                  "the screen that says the card is safe to remove")
        leaveTheWriterIdle()
    }

    // The Pi Connect step has its own handler on the same signal, which runs
    // the validation and sets the flag the container then reads. So these
    // drive the real states rather than assigning the flag: assigning it is
    // pointless, because the step's own handler clears it before deciding.

    function test_pi_connect_does_not_advance_with_no_token() {
        const step = goToStep(wiz.stepPiConnectCustomization)
        const toggle = findChild(step, "connectUseTokenToggle")
        verify(toggle, "found the Connect toggle")
        toggle.checked = true
        const tokenField = findChild(step, "connectTokenField")
        verify(tokenField, "found the token field")
        tokenField.text = ""
        wait(100)

        step.nextClicked()

        wait(200)
        compare(wiz.currentStep, wiz.stepPiConnectCustomization,
                "the step kept the user where the problem is")
    }

    function test_pi_connect_advances_when_it_is_not_being_used() {
        // Turned off there is nothing to validate and nothing to hold on,
        // and without this the case above would pass on a step that could
        // never be left.
        const step = goToStep(wiz.stepPiConnectCustomization)
        const toggle = findChild(step, "connectUseTokenToggle")
        verify(toggle, "found the Connect toggle")
        toggle.checked = false
        wait(100)

        step.nextClicked()

        tryVerify(function () {
            return wiz.currentStep !== wiz.stepPiConnectCustomization
        }, 3000, "the wizard moved on")
    }


    // -- Where the wizard begins -------------------------------------------
    //
    // The language step is a pre-step: it is the stack's initial item when
    // the application asks for it, and it cannot be jumped to, so its Next
    // handler had never run.
    //
    // What that handler decides is which screen the wizard starts on. Offline
    // there is no device list to choose from, so the board step is skipped;
    // landing on it anyway leaves the user on a page with nothing on it and
    // nothing to explain why. The two cases are the same assertion read
    // against whichever state this run is in, so the wrong branch fails
    // either way.

    Component {
        id: languageFirstComponent
        WizardContainer { showLanguageSelection: true }
    }

    function test_the_language_step_leads_to_the_right_first_screen() {
        const langWiz = createTemporaryObject(languageFirstComponent, testCase)
        verify(langWiz, "the wizard was created asking for the language step")
        langWiz.overlayRootRef = testCase
        wait(150)

        const stack = findStepStack(langWiz)
        verify(stack !== null, "the container has a step stack")
        const langStep = stack.currentItem
        verify(langStep !== null, "the language step is on it")

        langStep.nextClicked()
        wait(150)

        if (langWiz.hasNetworkConnectivity) {
            compare(langWiz.currentStep, langWiz.stepDeviceSelection,
                    "online, so the board is chosen first")
        } else {
            compare(langWiz.currentStep, langWiz.stepOSSelection,
                    "offline, so the board step is skipped -- there is no "
                    + "device list to choose from")
        }
    }

    function test_the_language_step_is_where_the_wizard_starts_when_asked() {
        // The premise of the case above: asking for it is what puts it in
        // front of everything else. Without that the language choice is
        // never offered at all.
        const langWiz = createTemporaryObject(languageFirstComponent, testCase)
        verify(langWiz)
        langWiz.overlayRootRef = testCase
        wait(150)

        const stack = findStepStack(langWiz)
        verify(stack !== null)
        verify(stack.currentItem !== null,
               "something is on the stack before anything was chosen")
        // Named, not merely present: a first draft checked only that the
        // stack had one item, which is true whichever step is on it, so it
        // held even with the language step taken out of the initial item.
        verify(findChild(stack.currentItem, "languageCombo") !== null,
               "and it is the language step, not the one after it")
        compare(stack.depth, 1, "with nothing behind it")
    }
}
