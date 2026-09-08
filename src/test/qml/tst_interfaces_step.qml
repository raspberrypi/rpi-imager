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
        // The step watches this for capability changes and calls the other
        // two on skip. Missing, the Connections block warns that no signal
        // matches and the skip path throws -- neither of which fails a test.
        property string selectedDeviceName: "Raspberry Pi 5"
        property string networkInfoText: ""
        property int stepWriting: 9
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
        property int advanced: 0
        function nextStep() { fakeContainer.advanced++ }
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


    // ── Answering the USB gadget warning ──────────────────────────────
    //
    // The cases above cover the warning being raised. What happens to the
    // answer was uncovered: both buttons, the escape key, the two-second
    // hold on the accept button, and the reset behind it.
    //
    // USB gadget mode changes how the board enumerates over USB, which is
    // what the warning is about, and the step only proceeds once
    // isConfirmed is set. So a dismissal that set it anyway would carry the
    // user past a warning they closed, and a hold that did not reset would
    // let them dismiss once and click straight through the second time --
    // which is the whole of what a deliberate delay is for.

    // updateCaps() is deferred with Qt.callLater and recomputes the
    // capability flags from the chosen OS, of which there is none here, so
    // it decides the step has nothing to offer and marks it already
    // confirmed. Anything that spins the event loop -- tryVerify, wait --
    // lets that run, so the flags are put back afterwards rather than in
    // init(). Which capabilities are detected is not what these cover.
    function armTheStep() {
        wait(150)
        step.supportsUsbOtg = true
        step.supportsI2c = true
        step.isConfirmed = false
        fakeContainer.advanced = 0
    }

    function usbGadgetOn() {
        armTheStep()
        child("enableUsbGadgetToggle").checked = true
        step.nextClicked()
        tryVerify(function () { return warning().opened }, 3000,
                  "the warning was raised")
        verify(!step.isConfirmed,
               "and nothing has been confirmed by raising it")
    }

    function acceptButton() {
        var b = findChild(step, "usbGadgetAcceptButton")
        verify(b, "found the accept button")
        return b
    }

    function test_the_accept_button_is_held_for_a_moment() {
        // Long enough that it cannot be part of the same click that opened
        // the dialog.
        usbGadgetOn()

        verify(!acceptButton().enabled,
               "there is nothing to click yet")
        verify(!step.isConfirmed)
    }

    function test_the_hold_ends_and_accepting_becomes_possible() {
        usbGadgetOn()

        tryVerify(function () { return acceptButton().enabled }, 5000,
                  "the button became available")
    }

    function test_cancelling_is_available_immediately() {
        // The safe answer is never held back.
        usbGadgetOn()

        verify(findChild(step, "usbGadgetCancelButton").enabled)
    }

    function test_accepting_confirms_and_moves_on() {
        usbGadgetOn()
        tryVerify(function () { return acceptButton().enabled }, 5000)

        acceptButton().clicked()

        verify(step.isConfirmed, "the warning was understood")
        compare(fakeContainer.advanced, 1, "and the step moved on")
        tryVerify(function () { return !warning().visible }, 3000)
    }

    function test_cancelling_does_not_confirm_or_move_on() {
        usbGadgetOn()

        findChild(step, "usbGadgetCancelButton").clicked()

        verify(!step.isConfirmed, "the warning was not answered")
        compare(fakeContainer.advanced, 0, "and the step stayed where it was")
        tryVerify(function () { return !warning().visible }, 3000)
    }

    function test_escape_does_not_confirm_either() {
        // Dismissing a warning is not reading it.
        usbGadgetOn()

        warning().escapePressed()

        verify(!step.isConfirmed)
        compare(fakeContainer.advanced, 0)
        tryVerify(function () { return !warning().visible }, 3000)
    }

    function test_a_second_showing_has_to_be_waited_out_again() {
        // Otherwise dismissing once and reopening is a way round the delay,
        // which leaves it protecting nobody.
        //
        // The reset happens in two places -- when the dialog closes and
        // again when it opens -- so removing the one in onClosed fails
        // nothing. It is the opening one this rests on.
        usbGadgetOn()
        tryVerify(function () { return acceptButton().enabled }, 5000)
        warning().escapePressed()
        tryVerify(function () { return !warning().visible }, 3000)

        step.supportsUsbOtg = true
        step.nextClicked()
        tryVerify(function () { return warning().opened }, 3000,
                  "the warning came up again")

        verify(!acceptButton().enabled,
               "and the hold started over rather than carrying on from "
               + "where the first showing left it")
    }

    // ── Going back and choosing a different board ─────────────────────
    //
    // The step is reached by picking a board, and which interfaces exist
    // depends on which board it is. Somebody who goes back and picks a
    // different one has to be offered what the new board has -- otherwise
    // the sidebar keeps offering an Interfaces step for hardware that is not
    // there, and the toggles inside it write settings the board cannot
    // honour.

    function test_choosing_a_different_board_re_derives_what_is_offered() {
        // The step re-reads its capabilities once when it is shown, on a
        // deferred call. That has to be allowed to happen before the ones
        // this case cares about are set, or it clears them afterwards and
        // the case passes whether the board change did anything or not --
        // which is how the first draft of this passed with the handler
        // under test removed entirely.
        wait(200)
        step.supportsUsbOtg = true
        step.supportsI2c = true
        fakeContainer.ifAndFeaturesAvailable = true
        verify(step.supportsUsbOtg, "the step starts with something supported")
        verify(step.supportsI2c, "and something else")

        // No OS is chosen in this harness, so the capabilities the singleton
        // reports for the new board are none. That is the point: the step has
        // to ask again rather than keep the answer it already had.
        fakeContainer.selectedDeviceName = "Raspberry Pi Zero 2 W"

        tryVerify(function () { return !step.supportsUsbOtg }, 3000,
                  "the step asked again rather than keeping the old answer")
        tryVerify(function () { return !fakeContainer.ifAndFeaturesAvailable }, 3000,
                  "and a board with nothing to offer leaves the step off the "
                  + "sidebar rather than offering an empty screen")
    }
}
