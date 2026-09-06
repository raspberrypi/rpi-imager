/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * IfAndFeaturesCustomizationStep: which hardware interfaces the image turns
 * on, and the third of the application's understanding confirmations.
 *
 * Two properties here are about not carrying settings where they do not
 * belong. Whether an interface can be enabled at all depends on the OS and
 * the board, so a box left ticked for one selection must not write itself
 * into an image built for another -- which is why nothing on this step is
 * persisted, and why stale values written by older versions are actively
 * removed on the way past.
 *
 * The third is the USB gadget warning, which follows the same rule as the
 * storage filter and passwordless sudo: raised for everyone, skipped for
 * the deployment-wide opt-out and for an assistive technology that has
 * already read the toggle's description aloud. The description carries the
 * dialog's wording, which is the condition on skipping it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "InterfacesStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property Item overlayRootRef: null
        property var customizationSettings: ({})
        property bool disableWarnings: false
        property bool ifI2cEnabled: false
        property bool ifSpiEnabled: false
        property bool if1WireEnabled: false
        property string ifSerial: "Disabled"
        property bool featUsbGadgetEnabled: false
        // The step writes these two on completion when the OS does not
        // advertise cc-rpi support; without them the assignments fail and
        // the step is left half-initialised.
        property bool ccRpiAvailable: true
        property bool ifAndFeaturesAvailable: true
    }

    Component {
        id: stepComponent
        IfAndFeaturesCustomizationStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function init() {
        TestAccessibility.setActive(false)
        fakeContainer.overlayRootRef = testCase
        fakeContainer.disableWarnings = false
        fakeContainer.customizationSettings = ({})
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        // updateCaps() asks the singleton what the selected OS supports, and
        // with no OS chosen that is nothing. Set them here so the step can
        // be driven; which capabilities are detected is not what is under
        // test.
        step.supportsUsbOtg = true
        step.supportsI2c = true
    }

    function cleanup() {
        TestAccessibility.setActive(false)
        if (step) {
            step.destroy()
            step = null
        }
    }

    function child(name) {
        var c = findChild(step, name)
        verify(c, "found " + name)
        return c
    }

    function warning() {
        var d = findChild(step, "usbGadgetWarningDialog")
        verify(d, "found the USB gadget warning")
        return d
    }

    // ── The USB gadget warning ────────────────────────────────────────

    function test_enabling_usb_gadget_mode_warns_first() {
        child("enableUsbGadgetToggle").checked = true

        step.nextClicked()

        tryVerify(function () { return warning().opened }, 3000,
                  "the warning was raised")
    }

    function test_leaving_usb_gadget_mode_alone_warns_about_nothing() {
        child("enableUsbGadgetToggle").checked = false

        step.nextClicked()

        wait(200)
        verify(!warning().opened)
    }

    function test_the_deployment_opt_out_skips_the_usb_gadget_warning() {
        fakeContainer.disableWarnings = true
        child("enableUsbGadgetToggle").checked = true

        step.nextClicked()

        wait(300)
        verify(!warning().opened)
    }

    function test_an_assistive_technology_skips_the_usb_gadget_warning() {
        TestAccessibility.setActive(true)
        tryVerify(function () { return PlatformHelper.assistiveTechnologyActive },
                  4000, "the accessibility state took effect")
        child("enableUsbGadgetToggle").checked = true

        step.nextClicked()

        wait(300)
        verify(!warning().opened, "the dialog was skipped for a screen reader")
    }

    function test_the_skipped_usb_gadget_warning_is_carried_by_the_toggle() {
        // Same condition as the sudo checkbox: the dialog may only be left
        // out because the wording is somewhere a screen reader will reach.
        var description = child("enableUsbGadgetToggle").accessibleDescription

        verify(description.length > 0, "the toggle carries a description")
        verify(description.indexOf("USB device mode") !== -1,
               "it says what is being turned on")
        verify(description.indexOf("only enable it if you are sure") !== -1,
               "and that it is not a casual choice")
    }

    // ── Settings that must not outlive the OS they were chosen for ────

    function test_an_unsupported_interface_is_written_off_whatever_the_box_says() {
        // The box can be left ticked from a previous selection. What the
        // image gets has to follow what this OS actually supports, or the
        // first boot enables an interface the kernel has no driver for.
        child("enableI2CToggle").checked = true
        step.supportsI2c = false

        step.nextClicked()

        compare(fakeContainer.customizationSettings.enableI2C, false)
        compare(fakeContainer.ifI2cEnabled, false)
    }

    function test_a_supported_interface_is_written_as_chosen() {
        child("enableI2CToggle").checked = true
        step.supportsI2c = true

        step.nextClicked()

        compare(fakeContainer.customizationSettings.enableI2C, true)
        compare(fakeContainer.ifI2cEnabled, true)
    }

    function test_interface_choices_are_not_kept_for_the_next_session_data() {
        return [
            { tag: "I2C",        key: "enableI2C" },
            { tag: "SPI",        key: "enableSPI" },
            { tag: "1-Wire",     key: "enable1Wire" },
            { tag: "serial",     key: "enableSerial" },
            { tag: "USB gadget", key: "enableUsbGadget" }
        ]
    }

    function test_interface_choices_are_not_kept_for_the_next_session(data) {
        // These depend on per-OS capability, so persisting them would carry
        // a choice made for one image into an image that cannot honour it.
        // Older versions did persist them, and the step clears those out.
        ImageWriterSingleton.setPersistedCustomisationSetting(data.key, true)

        step.nextClicked()

        var saved = ImageWriterSingleton.getSavedCustomisationSettings()
        verify(saved[data.key] === undefined,
               data.tag + " was not left in the saved settings")
    }
}
