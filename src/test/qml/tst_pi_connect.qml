/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * PiConnectCustomizationStep: the token that enrols the board.
 *
 * The token is pasted from the Connect website and written into the image,
 * so a bad one produces a board that comes up and never appears in the
 * user's account -- with nothing on the device or in the app to say why. The
 * step refuses to continue with one that cannot be right, and says so
 * rather than failing quietly later.
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


    // ── The hold on the token field ───────────────────────────────────
    //
    // Signing in happens in a browser: the user presses "Open Raspberry Pi
    // Connect", the site gives them a token, and they bring it back here.
    // The field they paste it into starts disabled and is released by a
    // countdown started when they leave for the browser, so it is not sitting
    // there empty and editable before there is anything to put in it.

    Component {
        id: shadowedStepComponent

        PiConnectCustomizationStep {
            id: shadowed
            wizardContainer: fakeContainer
            width: 900
            height: 700

            property int signInsOpened: 0
            function openConnectSignIn() { shadowed.signInsOpened++ }
        }
    }

    property var held: null

    function makeHeldStep() {
        held = createTemporaryObject(shadowedStepComponent, testCase)
        verify(held, "the step was created")
        findChild(held, "connectUseTokenToggle").checked = true
        waitForRendering(held)
        return held
    }

    function signInButton() {
        var b = findChild(held, "connectOpenSignInButton")
        verify(b, "found the sign-in button")
        return b
    }

    function countdown() {
        var t = findChild(held, "connectCountdownTimer")
        verify(t, "found the countdown")
        return t
    }

    function test_the_token_field_is_not_editable_before_signing_in() {
        makeHeldStep()

        verify(!held.tokenFieldEnabled,
               "there is nothing to paste yet")
    }

    function test_going_to_sign_in_starts_the_hold() {
        makeHeldStep()

        signInButton().clicked()

        compare(held.signInsOpened, 1, "the browser was sent for")
        verify(countdown().running, "and the hold started")
        compare(held.countdownSeconds, 25)
    }

    function test_the_hold_counts_down_and_releases_the_field() {
        // Driven from one second rather than twenty-five: what is under
        // test is that the count reaches zero and does something, not how
        // long the wait is.
        makeHeldStep()
        signInButton().clicked()
        held.countdownSeconds = 1

        tryVerify(function () { return held.tokenFieldEnabled }, 5000,
                  "the field the token goes in became editable")
        verify(!countdown().running, "and the countdown stopped")
        compare(held.countdownSeconds, 0)
    }

    function test_the_count_does_not_run_past_zero() {
        // The timer repeats, so the count reaching zero has to be the end
        // of it.
        //
        // Two things see to that -- a floor on the decrement and the
        // stop() at zero -- and the stop() alone is enough: removing the
        // floor fails nothing, because the timer never gets another tick.
        // What this pins is the outcome rather than either guard.
        makeHeldStep()
        signInButton().clicked()
        held.countdownSeconds = 1
        tryVerify(function () { return held.countdownSeconds === 0 }, 5000)

        wait(300)

        compare(held.countdownSeconds, 0, "it stayed at zero")
    }

    function test_going_back_to_sign_in_again_does_not_restart_the_hold() {
        // Pressing it twice is an ordinary thing to do when a browser takes
        // a moment to appear. Restarting the count each time would put the
        // field out of reach for another twenty-five seconds every press.
        makeHeldStep()
        signInButton().clicked()
        held.countdownSeconds = 5

        signInButton().clicked()

        compare(held.signInsOpened, 2, "the browser was sent for again")
        compare(held.countdownSeconds, 5,
                "and the hold carried on from where it was")
    }

    // ── Being told the token is wrong ─────────────────────────────────
    //
    // The complaint is only half of it. A user who is told their token is
    // invalid and finds the same token still in the field has no signal that
    // Imager rejected the one they can see, and pressing Next again gets the
    // same dialog. So dismissing the notice clears the field.

    function test_dismissing_the_invalid_token_notice_clears_the_field() {
        useToken("not-a-real-token")
        step.nextClicked()
        tryVerify(function() { return invalidDialog().opened }, 3000,
                  "the complaint was raised")

        mouseClick(child("connectInvalidTokenOkButton"))

        tryVerify(function() { return !invalidDialog().opened }, 3000,
                  "the notice closed")
        compare(child("connectTokenField").text, "",
                "the rejected token was cleared out of the field")
        compare(step.connectToken, "", "and out of the step")
        verify(!step.connectTokenReceived,
               "so nothing downstream thinks a token was accepted")
    }

    function test_escape_closes_the_invalid_token_notice() {
        // Closed with the keyboard as well as the button, because the dialog
        // takes focus and there is nothing else to press.
        useToken("not-a-real-token")
        step.nextClicked()
        tryVerify(function() { return invalidDialog().opened }, 3000)

        invalidDialog().escapePressed()

        tryVerify(function() { return !invalidDialog().opened }, 3000,
                  "escape closed the notice")
    }

    // ── Organisation mode with nothing to mint from ───────────────────
    //
    // Organisation mode writes a single-use auth key into the image, minted
    // from the organisation's API key when Next is pressed. With no key
    // stored there is nothing to mint from, and the refusal has to reach the
    // screen -- the alternative is a Next button that appears to do nothing.
    //
    // The refusal itself is C++ and has its own cases. What is checked here
    // is that it arrives: the dialog opens, and it carries the message the
    // writer gave rather than a blank panel.

    function orgStep() {
        // orgModeEnabled is a binding on a settings read with no notify
        // signal, so the setting has to be in place before the step is built.
        ImageWriterSingleton.clearConnectOrgRegistration()
        ImageWriterSingleton.setSetting("connect_org_enabled", true)
        if (step) {
            step.destroy()
            step = null
        }
        step = stepComponent.createObject(testCase)
        verify(step, "the organisation-mode step was created")
        verify(step.orgModeEnabled, "the step is in organisation mode")
        return step
    }

    function endOrgMode() {
        // Session-wide, and every file in the run shares these settings.
        ImageWriterSingleton.setSetting("connect_org_enabled", false)
        ImageWriterSingleton.clearConnectOrgRegistration()
    }

    function authKeyDialog() {
        var d = findChild(step, "connectAuthKeyErrorDialog")
        verify(d, "found the auth-key failure dialog")
        return d
    }

    function test_org_mode_with_no_api_key_says_why_it_cannot_continue() {
        orgStep()
        verify(!ImageWriterSingleton.hasConnectOrgRegistration(),
               "there is no organisation key to mint from")

        step.nextClicked()

        tryVerify(function() { return authKeyDialog().opened }, 5000,
                  "the failure was put on screen")
        verify(authKeyDialog().detail.length > 0,
               "and it carries the reason, not an empty panel")
        verify(!step.isValid,
               "the step is not treated as configured")

        endOrgMode()
    }

    function test_escape_closes_the_minting_failure_notice() {
        orgStep()
        step.nextClicked()
        tryVerify(function() { return authKeyDialog().opened }, 5000)

        authKeyDialog().escapePressed()

        tryVerify(function() { return !authKeyDialog().opened }, 3000,
                  "escape closed the failure notice")

        endOrgMode()
    }

    // ── Where the switch sits in the tab ring ─────────────────────────
    //
    // The ring a wizard step builds is Next, then Back, Skip and App
    // Options, and only then the page's own controls. On a page that is one
    // switch and what follows from it, that leaves the switch four presses
    // from the start -- so this step asks for its controls to come straight
    // after Next instead.

    // The ring as it is actually wired, walked along KeyNavigation.tab.
    function tabRing() {
        var seen = []
        var cur = step.nextButtonItem
        for (var i = 0; i < 30 && cur && seen.indexOf(cur) === -1; i++) {
            seen.push(cur)
            cur = cur.KeyNavigation.tab
        }
        return seen
    }

    function test_the_switch_comes_straight_after_next() {
        waitForRendering(step)
        var ring = tabRing()
        verify(ring.length > 2, "the ring was walked")
        compare(ring[0], step.nextButtonItem, "the walk started at Next")
        compare(ring[1], child("connectUseTokenToggle").focusItem,
                "the switch is the next thing reached")
    }

    function test_the_secondary_buttons_come_after_the_page_controls() {
        // Back and Skip still exist, and still come round -- behind the
        // controls rather than in front of them.
        waitForRendering(step)
        var ring = tabRing()
        var pill = ring.indexOf(child("connectUseTokenToggle").focusItem)
        var back = ring.indexOf(step.backButtonItem)
        var skip = ring.indexOf(step.skipButtonItem)
        verify(pill !== -1, "the switch is in the ring")
        verify(back !== -1, "so is Back")
        verify(skip !== -1, "and Skip")
        verify(pill < back, "the switch comes before Back")
        verify(back < skip, "and Back still comes before Skip")
    }

    function test_turning_it_on_keeps_its_own_controls_together() {
        // Enabling reveals the sign-in button, which is the next thing to do
        // on the page. It belongs with the switch, ahead of Back, not on the
        // far side of the secondary buttons.
        var pill = child("connectUseTokenToggle")
        mouseClick(pill.focusItem)
        tryVerify(function () { return pill.checked }, 3000, "Connect is on")
        waitForRendering(step)

        var ring = tabRing()
        var back = ring.indexOf(step.backButtonItem)
        var signIn = ring.indexOf(child("connectOpenSignInButton"))
        verify(signIn !== -1, "the sign-in button is reachable")
        verify(back !== -1, "and Back is still in the ring")
        verify(signIn < back, "with sign-in ahead of it")
    }
}
