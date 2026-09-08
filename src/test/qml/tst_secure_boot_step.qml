/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * SecureBootCustomizationStep: signing the image, and the key it is signed
 * with.
 *
 * Secure boot is the one customisation setting whose failure mode is a
 * board that will not start. A device whose OTP has been programmed boots
 * only images signed with the matching key, so an image written without
 * signing -- because the switch was not recorded, or the key was recorded
 * in a form the signer cannot read -- leaves the user with hardware that
 * shows nothing and cannot be put back.
 *
 * The step had no test at all. Four handlers were uncovered: what leaving
 * the step writes, the key chosen through the fallback picker, the button
 * that opens it, and the focus rebuild behind the switch.
 *
 * The OTP provisioning section is deliberately not covered. It is gated off
 * in the source -- otpProvisioningImplemented is false, and the comment
 * there says why -- so nothing a user can reach goes through it.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "SecureBootStep"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property bool secureBootAvailable: true
        property bool secureBootEnabled: false
        property bool secureBootKeyConfigured: false
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property string networkInfoText: ""
        property int stepWriting: 11
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
        function skipAllCustomisation() { jumpToStep(stepWriting) }
    }

    Component {
        id: stepComponent
        SecureBootCustomizationStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    function init() {
        fakeContainer.customizationSettings = ({})
        fakeContainer.secureBootEnabled = false
        fakeContainer.secureBootKeyConfigured = false
        ImageWriterSingleton.clearSavedCustomisationSettings()
        ImageWriterSingleton.setSetting("secureboot_rsa_key", "")
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
    }

    function cleanup() {
        if (step) {
            step.destroy()
            step = null
        }
        ImageWriterSingleton.clearSavedCustomisationSettings()
        ImageWriterSingleton.setSetting("secureboot_rsa_key", "")
    }

    function child(name) {
        var c = findChild(step, name)
        verify(c, "found " + name)
        return c
    }

    function saved() {
        return ImageWriterSingleton.getSavedCustomisationSettings()
    }

    // ── What leaving the step writes ──────────────────────────────────

    function test_turning_secure_boot_on_is_recorded() {
        // Both places: the map the generator reads, and the container flag
        // the later steps and the summary read. A switch that moves and is
        // not recorded means an unsigned image written to a board that will
        // only accept a signed one.
        child("secureBootEnablePill").checked = true

        step.nextClicked()

        compare(fakeContainer.customizationSettings.secureBootEnabled, true)
        verify(fakeContainer.secureBootEnabled, "and the container agrees")
    }

    function test_turning_secure_boot_on_survives_the_session() {
        // Persisted, so someone imaging a batch of boards does not have to
        // set it again for each one.
        child("secureBootEnablePill").checked = true

        step.nextClicked()

        compare(saved().secureBootEnabled, true)
    }

    function test_leaving_it_off_records_nothing() {
        // Removed rather than written as false: the generator treats the
        // key being present at all as the request.
        child("secureBootEnablePill").checked = false

        step.nextClicked()

        verify(fakeContainer.customizationSettings.secureBootEnabled === undefined,
               "it is absent rather than set to false")
        verify(!fakeContainer.secureBootEnabled)
        verify(saved().secureBootEnabled === undefined)
    }

    function test_turning_it_back_off_removes_what_was_recorded() {
        // The direction that matters for a batch: having signed one image,
        // the setting has to be droppable for the next.
        child("secureBootEnablePill").checked = true
        step.nextClicked()
        compare(saved().secureBootEnabled, true)

        child("secureBootEnablePill").checked = false
        step.nextClicked()

        verify(fakeContainer.customizationSettings.secureBootEnabled === undefined,
               "the runtime setting was dropped")
        verify(!fakeContainer.secureBootEnabled)
        verify(saved().secureBootEnabled === undefined,
               "and so was the persisted one")
    }

    // ── The signing key ───────────────────────────────────────────────

    function test_a_key_chosen_from_the_fallback_picker_is_recorded() {
        // The picker hands back a file:// url; the signer wants a path.
        // Left as a url it is stored, and signing fails later with nothing
        // pointing back at this screen.
        var picker = child("secureBootKeyFileDialog")

        picker.selectedFile = "file:///home/pi/.ssh/secureboot.pem"
        picker.accepted()

        compare(ImageWriterSingleton.getStringSetting("secureboot_rsa_key"),
                "/home/pi/.ssh/secureboot.pem")
        compare(step.rsaKeyPath, "/home/pi/.ssh/secureboot.pem",
                "and the screen shows the same thing")
        verify(fakeContainer.secureBootKeyConfigured,
               "and the wizard knows there is a key")
    }

    function test_choosing_nothing_leaves_the_key_alone() {
        // An empty answer from the picker must not clear a key that was
        // already set: that would leave secure boot on with nothing to sign
        // with, which fails at the far end of a long write.
        ImageWriterSingleton.setSetting("secureboot_rsa_key", "/home/pi/keep.pem")
        var picker = child("secureBootKeyFileDialog")

        picker.selectedFile = ""
        picker.accepted()

        compare(ImageWriterSingleton.getStringSetting("secureboot_rsa_key"),
                "/home/pi/keep.pem")
    }

    function test_the_key_button_opens_a_picker() {
        // Nothing else on this step can be used until a key is chosen -- the
        // enable pill stays disabled without one -- so a button that opened
        // nothing would leave secure boot unreachable with no error saying
        // why.
        //
        // The harness forces the in-app picker, the way --qml-file-dialogs
        // does, so this runs whether or not the machine has a native one.
        child("secureBootKeyButton").clicked()

        tryVerify(function () {
            return child("secureBootKeyFileDialog").opened
        }, 3000, "the picker came up")
        child("secureBootKeyFileDialog").close()
    }

    // ── Skipping ──────────────────────────────────────────────────────

    function test_skipping_here_skips_all_of_it() {
        fakeContainer.jumpedTo = -1

        step.skipClicked()

        compare(fakeContainer.jumpedTo, fakeContainer.stepWriting)
    }
}
