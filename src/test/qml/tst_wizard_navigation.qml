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
