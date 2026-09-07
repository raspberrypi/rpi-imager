/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * WifiCustomizationStep: what credentials reach the image.
 *
 * The existing Wi-Fi file covers what the fields accept. This covers what
 * leaving the step writes, which is where the failures are: a Pi that never
 * joins the network, discovered after first boot with no way to see why.
 *
 * Two properties carry most of the weight. The passphrase is never stored
 * -- only the PSK derived from it, because the settings map is long-lived
 * and persisted while the password field is destroyed on navigation. And a
 * stored PSK belongs to the SSID it was derived for: keeping it across a
 * change of network would write one network's key against another's name,
 * which cannot work and cannot be diagnosed from the image.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "WifiCommit"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property bool wifiConfigured: false
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool sshEnabled: false
        property string networkInfoText: ""
        property int stepWriting: 9
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
    }

    Component {
        id: stepComponent
        WifiCustomizationStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    readonly property string passphrase: "correct horse battery"

    function init() {
        fakeContainer.customizationSettings = ({})
        fakeContainer.wifiConfigured = false
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
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

    function enter(ssid, pwd) {
        child("wifiSsidField").text = ssid
        child("wifiPasswordField").text = pwd
        child("wifiPasswordConfirmField").text = pwd
    }

    // ── The passphrase must not be stored ─────────────────────────────

    function test_only_the_derived_key_is_stored() {
        enter("My Network", passphrase)

        step.nextClicked()

        var s = fakeContainer.customizationSettings
        compare(s.wifiSSID, "My Network")
        verify(s.wifiPasswordCrypt, "a key was derived")
        verify(s.wifiPasswordCrypt !== passphrase, "and it is not the passphrase")
        for (var key in s)
            verify(String(s[key]) !== passphrase,
                   "no key holds the passphrase (" + key + ")")
        verify(fakeContainer.wifiConfigured)
    }

    function test_the_passphrase_is_not_persisted_either() {
        // The saved settings outlive the session and go to disk.
        enter("My Network", passphrase)

        step.nextClicked()

        var saved = ImageWriterSingleton.getSavedCustomisationSettings()
        verify(String(JSON.stringify(saved)).indexOf(passphrase) === -1,
               "the passphrase is nowhere in the saved settings")
        verify(saved.wifiPasswordCrypt, "but the derived key is")
    }

    // ── A key belongs to its network ──────────────────────────────────

    function test_an_open_network_never_carries_a_key() {
        // Switching a configured network to open has to drop the key, or the
        // image describes an open network with a passphrase attached.
        enter("My Network", passphrase)
        step.nextClicked()
        verify(fakeContainer.customizationSettings.wifiPasswordCrypt)

        step.wifiMode = "open"
        step.nextClicked()

        verify(fakeContainer.customizationSettings.wifiPasswordCrypt === undefined)
    }

    function test_revisiting_the_same_network_keeps_the_saved_key() {
        // Coming back to this step shows the password blank, because only the
        // derived key was kept. Blank has to mean "keep it" for the same
        // network, or every revisit silently drops the credentials.
        enter("My Network", passphrase)
        step.nextClicked()
        var first = fakeContainer.customizationSettings.wifiPasswordCrypt
        verify(first)

        enter("My Network", "")
        step.nextClicked()

        compare(fakeContainer.customizationSettings.wifiPasswordCrypt, first,
                "the key survived a revisit")
    }

    function test_changing_network_without_a_password_drops_the_old_key() {
        // The sharp one. Keeping it would write the first network's key
        // against the second network's name -- credentials that cannot work
        // and cannot be explained by looking at the image.
        enter("My Network", passphrase)
        step.nextClicked()
        verify(fakeContainer.customizationSettings.wifiPasswordCrypt)

        enter("A Different Network", "")
        step.nextClicked()

        compare(fakeContainer.customizationSettings.wifiSSID, "A Different Network")
        verify(fakeContainer.customizationSettings.wifiPasswordCrypt === undefined,
               "the previous network's key was not carried over")
    }

    function test_a_new_passphrase_replaces_the_old_key() {
        enter("My Network", passphrase)
        step.nextClicked()
        var first = fakeContainer.customizationSettings.wifiPasswordCrypt

        enter("My Network", "an entirely different passphrase")
        step.nextClicked()

        verify(fakeContainer.customizationSettings.wifiPasswordCrypt !== first,
               "the key was re-derived")
    }

    // ── Clearing the network ──────────────────────────────────────────

    function test_clearing_the_ssid_removes_everything() {
        enter("My Network", passphrase)
        child("wifiHiddenToggle").checked = true
        step.nextClicked()
        verify(fakeContainer.wifiConfigured)

        enter("", "")
        step.nextClicked()

        var s = fakeContainer.customizationSettings
        verify(s.wifiSSID === undefined, "the network name is gone")
        verify(s.wifiPasswordCrypt === undefined, "so is the key")
        verify(s.wifiSsidOctetsBase64 === undefined)
        verify(s.wifiHidden === undefined)
        verify(!fakeContainer.wifiConfigured)
    }

    // ── A hidden network, and one whose name is not text ──────────────

    function test_a_hidden_network_is_recorded_as_hidden() {
        // Without this the Pi will not find the network at all: it has to be
        // told to probe for one that does not advertise itself.
        enter("My Network", passphrase)
        child("wifiHiddenToggle").checked = true

        step.nextClicked()

        compare(fakeContainer.customizationSettings.wifiHidden, true)
    }

    function test_the_ssid_is_also_recorded_as_octets() {
        // An SSID is bytes, not text. The octet form is what survives a name
        // that is not valid UTF-8, which the plain string would mangle.
        enter("My Network", passphrase)

        step.nextClicked()

        var octets = fakeContainer.customizationSettings.wifiSsidOctetsBase64
        verify(octets && octets.length > 0, "the octets were recorded")
        verify(octets !== "My Network", "and they are encoded, not the name")
    }

    function test_a_mismatched_confirmation_writes_nothing() {
        // Next is disabled in this state, but the handler is reachable by
        // other routes and must not store half a credential.
        child("wifiSsidField").text = "My Network"
        child("wifiPasswordField").text = passphrase
        child("wifiPasswordConfirmField").text = "something else"

        step.nextClicked()

        verify(fakeContainer.customizationSettings.wifiSSID === undefined,
               "nothing was written")
        verify(!fakeContainer.wifiConfigured)
    }
}
