/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * PiConnectCustomizationStep: the token that enrols the board.
 *
 * The token is pasted from the Connect website and written into the image,
 * so a bad one produces a board that comes up and never appears in the
 * user's account -- with nothing on the device or in the app to say why. The
 * step refuses to continue with one that cannot be right, and says so
 * rather than failing quietly later.
 *
 * The other property is that none of this is persisted. The token is
 * session-only, so a later run must not inherit one: it would enrol the
 * next board someone images into an account they were not thinking about.
 *
 * The organisation flows are not covered here -- one mints a key over the
 * network and the other registers at flash time -- so this is the per-user
 * token path only.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "PiConnect"
    when: windowShown
    width: 900
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property Item overlayRootRef: null
        property var customizationSettings: ({})
        property bool piConnectAvailable: true
        property bool piConnectEnabled: false
        property bool targetIsFastboot: false
        property string connectOrgDescription: ""
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
        PiConnectCustomizationStep {
            wizardContainer: fakeContainer
            width: 900
            height: 700
        }
    }

    property var step: null

    // 24 Base58 characters, the length the current tokens carry.
    readonly property string goodToken: "rpuak_abcdefghijkmnpqrstuvwxyz"

    function init() {
        fakeContainer.overlayRootRef = testCase
        fakeContainer.customizationSettings = ({})
        fakeContainer.piConnectEnabled = false
        fakeContainer.targetIsFastboot = false
        ImageWriterSingleton.clearConnectToken()
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
    }

    function cleanup() {
        ImageWriterSingleton.clearConnectToken()
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

    function invalidDialog() {
        var d = findChild(step, "connectInvalidTokenDialog")
        verify(d, "found the invalid-token dialog")
        return d
    }

    function useToken(text) {
        child("connectUseTokenToggle").checked = true
        child("connectTokenField").text = text
        step.connectToken = text
    }

    // ── Which tokens get through ──────────────────────────────────────

    function test_a_well_formed_token_is_accepted() {
        useToken(goodToken)

        step.nextClicked()

        wait(200)
        verify(!invalidDialog().opened, "no complaint was raised")
        compare(fakeContainer.customizationSettings.piConnectEnabled, true)
        verify(fakeContainer.piConnectEnabled)
    }

    function test_no_token_is_refused_out_loud() {
        // Silently continuing would write an image with Connect enabled and
        // nothing to enrol with.
        useToken("")

        step.nextClicked()

        tryVerify(function () { return invalidDialog().opened }, 3000,
                  "the user was told")
        verify(fakeContainer.customizationSettings.piConnectEnabled === undefined,
               "and nothing was configured")
    }

    function test_a_malformed_token_is_refused_out_loud_data() {
        return [
            { tag: "not a token",     token: "hello" },
            { tag: "wrong prefix",    token: "rpxak_abcdefghijkmnpqrstuvwxyz" },
            { tag: "too short",       token: "rpuak_abcdefghijkmnpqrstuvwxy" },
            // Base58 leaves out 0, O, I and l because they are hard to tell
            // apart; one appearing means the token was retyped, not pasted.
            { tag: "ambiguous digit", token: "rpuak_0bcdefghijkmnpqrstuvwxyz" }
        ]
    }

    function test_a_malformed_token_is_refused_out_loud(data) {
        useToken(data.token)

        step.nextClicked()

        tryVerify(function () { return invalidDialog().opened }, 3000,
                  "refused: " + data.tag)
        verify(fakeContainer.customizationSettings.piConnectEnabled === undefined)
    }

    function test_turning_connect_off_removes_it() {
        useToken(goodToken)
        step.nextClicked()
        wait(200)
        compare(fakeContainer.customizationSettings.piConnectEnabled, true)

        child("connectUseTokenToggle").checked = false
        step.nextClicked()

        verify(fakeContainer.customizationSettings.piConnectEnabled === undefined)
        verify(!fakeContainer.piConnectEnabled)
    }

    // ── It must not outlive the session ───────────────────────────────

    function test_the_token_is_never_persisted() {
        // Session-only by design: a later run inheriting it would enrol the
        // next board someone images into an account nobody chose.
        useToken(goodToken)

        step.nextClicked()
        wait(200)
        verify(fakeContainer.piConnectEnabled)

        var saved = ImageWriterSingleton.getSavedCustomisationSettings()
        verify(saved.piConnectEnabled === undefined,
               "the flag is not in the saved settings")
        verify(String(JSON.stringify(saved)).indexOf("rpuak_") === -1,
               "and the token itself is nowhere in them")
    }
}
