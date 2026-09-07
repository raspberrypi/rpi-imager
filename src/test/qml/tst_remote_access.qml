/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * RemoteAccessStep: whether, and how, you can reach the Pi afterwards.
 *
 * The failure this step has to prevent is the same one the account step
 * does, arrived at from the other side: an image with SSH switched on,
 * public-key authentication chosen and no key to authorise. Password login
 * is off in that configuration, so nothing can log in, and the image looks
 * perfectly fine until the board is up and refusing connections. Next is
 * disabled for it, and nothing checked that.
 *
 * The step was at 0%.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "RemoteAccess"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property string networkInfoText: ""
        property int stepWriting: 9
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
    }

    Component {
        id: stepComponent
        RemoteAccessStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    readonly property string aKey: "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI someone@host"

    function init() {
        fakeContainer.customizationSettings = ({})
        fakeContainer.sshEnabled = false
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        keys().keys = []
    }

    function cleanup() {
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

    function keys() { return child("sshKeyManager") }

    // ── When the step will let you leave ──────────────────────────────

    function test_leaving_ssh_off_may_proceed() {
        child("sshEnableToggle").checked = false
        verify(step.nextButtonEnabled)
    }

    function test_password_authentication_may_proceed() {
        child("sshEnableToggle").checked = true
        child("sshPasswordAuthRadio").checked = true
        verify(step.nextButtonEnabled)
    }

    function test_public_key_authentication_with_no_key_may_not_proceed() {
        // The one that matters. SSH on, passwords off, nothing authorised:
        // an image nobody can log into, discovered after first boot.
        child("sshEnableToggle").checked = true
        child("sshPublicKeyAuthRadio").checked = true
        keys().keys = []

        verify(!step.nextButtonEnabled)
    }

    function test_public_key_authentication_with_a_key_may_proceed() {
        child("sshEnableToggle").checked = true
        child("sshPublicKeyAuthRadio").checked = true
        keys().addKey(aKey)

        verify(step.nextButtonEnabled)
    }

    // ── What leaving the step writes ──────────────────────────────────

    function test_password_authentication_writes_no_keys() {
        child("sshEnableToggle").checked = true
        child("sshPasswordAuthRadio").checked = true

        step.nextClicked()

        var s = fakeContainer.customizationSettings
        compare(s.sshEnabled, true)
        compare(s.sshPasswordAuth, true)
        verify(s.sshAuthorizedKeys === undefined, "no keys were written")
        verify(fakeContainer.sshEnabled)
    }

    function test_public_key_authentication_writes_the_keys() {
        child("sshEnableToggle").checked = true
        child("sshPublicKeyAuthRadio").checked = true
        keys().addKey(aKey)

        step.nextClicked()

        var s = fakeContainer.customizationSettings
        compare(s.sshEnabled, true)
        compare(s.sshPasswordAuth, false)
        compare(s.sshAuthorizedKeys, aKey)
        verify(s.sshPublicKey === undefined,
               "and the single-key setting older versions wrote is cleared")
    }

    function test_turning_ssh_off_removes_everything_it_had_set() {
        // Otherwise an image built after changing your mind still has SSH
        // enabled, from settings the user believes they turned off.
        child("sshEnableToggle").checked = true
        child("sshPublicKeyAuthRadio").checked = true
        keys().addKey(aKey)
        step.nextClicked()
        verify(fakeContainer.customizationSettings.sshEnabled)

        child("sshEnableToggle").checked = false
        step.nextClicked()

        var s = fakeContainer.customizationSettings
        verify(s.sshEnabled === undefined, "sshEnabled cleared")
        verify(s.sshPasswordAuth === undefined, "sshPasswordAuth cleared")
        verify(s.sshAuthorizedKeys === undefined, "the keys cleared")
        verify(s.sshPublicKey === undefined, "the legacy key cleared")
        verify(!fakeContainer.sshEnabled)
    }

    function test_switching_back_to_passwords_drops_the_keys() {
        // The keys stay in the manager so they are there if the user changes
        // back, but they must not reach an image configured for passwords.
        child("sshEnableToggle").checked = true
        child("sshPublicKeyAuthRadio").checked = true
        keys().addKey(aKey)
        step.nextClicked()
        compare(fakeContainer.customizationSettings.sshAuthorizedKeys, aKey)

        child("sshPasswordAuthRadio").checked = true
        step.nextClicked()

        verify(fakeContainer.customizationSettings.sshAuthorizedKeys === undefined)
        compare(fakeContainer.customizationSettings.sshPasswordAuth, true)
    }
}
