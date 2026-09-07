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
}
