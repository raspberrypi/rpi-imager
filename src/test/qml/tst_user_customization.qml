/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * UserCustomizationStep: the account the Pi is created with.
 *
 * Two things go wrong here and both are discovered after the card is in the
 * board. Letting the user past this step without a usable password produces
 * an image whose account cannot be logged into, with no way back except
 * writing the card again. And the plaintext password must never reach the
 * settings map -- only the hash does, because the map is long-lived and gets
 * persisted, while the field holding the plaintext is destroyed on
 * navigation.
 *
 * The step was at 0% before this file: nothing instantiated it, so neither
 * the rule about when Next lights up nor the rule about what gets written
 * had ever run in a test.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "UserCustomization"
    when: windowShown
    width: 800
    height: 600
    visible: true

    // Stands in for WizardContainer. The step only reads these, and a real
    // container drags in every other step with it.
    QtObject {
        id: fakeContainer
        property var customizationSettings: ({})
        property bool userConfigured: false
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
        property bool passwordlessSudoAvailable: true
        property bool disableWarnings: false
        property string networkInfoText: ""
        property Item overlayRootRef: null
        property int stepWriting: 9
        property int jumpedTo: -1
        function jumpToStep(n) { jumpedTo = n }
    }

    Component {
        id: stepComponent
        UserCustomizationStep {
            wizardContainer: fakeContainer
            width: 800
            height: 600
        }
    }

    property var step: null

    function init() {
        TestAccessibility.setActive(false)
        fakeContainer.overlayRootRef = testCase
        fakeContainer.disableWarnings = false
        fakeContainer.customizationSettings = ({})
        fakeContainer.userConfigured = false
        fakeContainer.hostnameConfigured = true
        fakeContainer.localeConfigured = true
        fakeContainer.wifiConfigured = true
        fakeContainer.sshEnabled = true
        fakeContainer.jumpedTo = -1
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        // A saved password from a previous session changes every rule below,
        // so each case starts without one and opts in.
        step.hasSavedUserPassword = false
    }

    function cleanup() {
        TestAccessibility.setActive(false)
        if (step) {
            step.destroy()
            step = null
        }
    }

    function field(name) {
        var f = findChild(step, name)
        verify(f, "found " + name)
        return f
    }

    function fill(username, password, confirm) {
        field("userNameField").text = username
        field("userPasswordField").text = password
        field("userPasswordConfirmField").text = confirm
    }

    // ── When the step will let you leave ──────────────────────────────

    function test_nothing_entered_may_proceed() {
        // Skipping user configuration entirely is legitimate; the image just
        // gets no account.
        fill("", "", "")
        verify(step.nextButtonEnabled)
    }

    function test_a_username_with_no_password_may_not_proceed() {
        // The case that matters. An account with no password is an account
        // nobody can log into, and it is only discovered after first boot.
        fill("pi", "", "")
        verify(!step.nextButtonEnabled)
    }

    function test_passwords_that_do_not_match_may_not_proceed() {
        fill("pi", "correct horse", "correct hoarse")
        verify(!step.nextButtonEnabled)
    }

    function test_a_password_with_no_confirmation_may_not_proceed() {
        fill("pi", "correct horse", "")
        verify(!step.nextButtonEnabled)
    }

    function test_matching_passwords_may_proceed() {
        fill("pi", "correct horse", "correct horse")
        verify(step.nextButtonEnabled)
    }

    function test_a_saved_password_may_be_kept_by_leaving_the_fields_blank() {
        // Returning to this step in a later session shows the password blank
        // because only the hash was kept. Blank has to mean "keep it" rather
        // than "clear it", or every revisit silently drops the account.
        step.hasSavedUserPassword = true
        fill("pi", "", "")
        verify(step.nextButtonEnabled)
    }

    // ── What leaving the step writes ──────────────────────────────────

    function test_a_new_password_is_stored_hashed_and_never_in_the_clear() {
        // The settings map outlives the field and is persisted to disk. The
        // plaintext must not be in it under any key.
        fill("pi", "correct horse", "correct horse")
        step.nextClicked()

        var s = fakeContainer.customizationSettings
        compare(s.sshUserName, "pi")
        verify(s.sshUserPassword, "a password was stored")
        verify(s.sshUserPassword !== "correct horse", "the stored password is not the plaintext")
        for (var key in s)
            verify(String(s[key]) !== "correct horse",
                   "no key holds the plaintext (" + key + ")")
        verify(fakeContainer.userConfigured)
    }

    function test_clearing_every_field_removes_the_account() {
        fill("pi", "correct horse", "correct horse")
        step.nextClicked()
        verify(fakeContainer.userConfigured)

        fill("", "", "")
        step.nextClicked()

        var s = fakeContainer.customizationSettings
        verify(s.sshUserName === undefined, "the username was removed")
        verify(s.sshUserPassword === undefined, "the password was removed")
        verify(!fakeContainer.userConfigured)
    }

    function test_a_half_filled_step_configures_nothing() {
        // Next is disabled in this state, but the handler is reachable by
        // other routes and must not write half an account.
        fill("pi", "correct horse", "")
        step.nextClicked()
        verify(!fakeContainer.userConfigured)
    }

    // ── Passwordless sudo ─────────────────────────────────────────────

    function test_passwordless_sudo_is_off_unless_asked_for() {
        // Never loaded from persisted settings: it has to be chosen again
        // each session, so an image built today cannot inherit it from one
        // built months ago.
        fill("pi", "correct horse", "correct horse")
        step.nextClicked()
        verify(fakeContainer.customizationSettings.passwordlessSudo === undefined)
    }

    function test_passwordless_sudo_is_recorded_when_asked_for() {
        field("passwordlessSudoCheck").checked = true
        fill("pi", "correct horse", "correct horse")
        step.nextClicked()
        compare(fakeContainer.customizationSettings.passwordlessSudo, true)
    }

    function test_turning_passwordless_sudo_back_off_removes_it() {
        field("passwordlessSudoCheck").checked = true
        fill("pi", "correct horse", "correct horse")
        step.nextClicked()
        compare(fakeContainer.customizationSettings.passwordlessSudo, true)

        field("passwordlessSudoCheck").checked = false
        step.nextClicked()
        verify(fakeContainer.customizationSettings.passwordlessSudo === undefined,
               "it is removed rather than left set to false")
    }

    // ── Skipping customisation ────────────────────────────────────────

    function test_skipping_clears_every_customisation_and_goes_to_writing() {
        // Skip is offered from this step but disowns the whole customisation
        // section, not just the account. Leaving any of these set writes
        // configuration the user just declined -- a hostname or a Wi-Fi
        // network they thought they had backed out of.
        fill("pi", "correct horse", "correct horse")

        step.skipClicked()

        verify(!fakeContainer.hostnameConfigured, "hostname was cleared")
        verify(!fakeContainer.localeConfigured, "locale was cleared")
        verify(!fakeContainer.userConfigured, "user was cleared")
        verify(!fakeContainer.wifiConfigured, "wifi was cleared")
        verify(!fakeContainer.sshEnabled, "ssh was cleared")
        compare(fakeContainer.jumpedTo, fakeContainer.stepWriting,
                "and it goes straight to writing")
    }

    // ── The warning before passwordless sudo ──────────────────────────
    //
    // Passwordless sudo lets any process running as this user become root
    // without authentication. Ticking the box raises an understanding
    // confirmation, and that confirmation is skipped in exactly two cases:
    // the deployment-wide opt-out, and an assistive technology having
    // already read the risk out as the checkbox's description.
    //
    // The second is only defensible because the wording is relocated rather
    // than dropped. If the description were ever emptied, the skip would
    // remove the warning outright for precisely the users who cannot see
    // the checkbox they are ticking.

    function sudoWarning() {
        var d = findChild(step, "passwordlessSudoWarning")
        verify(d, "found the sudo warning dialog")
        return d
    }

    function test_enabling_passwordless_sudo_warns_first() {
        field("passwordlessSudoCheck").checked = true

        tryVerify(function () { return sudoWarning().opened }, 3000,
                  "the warning was raised")
    }

    function test_turning_it_back_off_does_not_warn() {
        // Making things safer again is not something to interrupt.
        field("passwordlessSudoCheck").checked = true
        tryVerify(function () { return sudoWarning().opened }, 3000)
        sudoWarning().close()
        tryVerify(function () { return !sudoWarning().opened }, 3000)

        field("passwordlessSudoCheck").checked = false

        wait(200)
        verify(!sudoWarning().opened)
    }

    function test_cancelling_the_warning_unticks_the_box() {
        // Otherwise the box reads as enabled while the setting was refused.
        var check = field("passwordlessSudoCheck")
        check.checked = true
        tryVerify(function () { return sudoWarning().opened }, 3000)

        sudoWarning().cancelled()

        tryVerify(function () { return !check.checked }, 3000)
    }

    function test_the_deployment_opt_out_skips_the_warning() {
        fakeContainer.disableWarnings = true

        field("passwordlessSudoCheck").checked = true

        wait(300)
        verify(!sudoWarning().opened)
    }

    function test_an_assistive_technology_skips_the_warning() {
        TestAccessibility.setActive(true)
        tryVerify(function () { return PlatformHelper.assistiveTechnologyActive },
                  4000, "the accessibility state took effect")

        field("passwordlessSudoCheck").checked = true

        wait(300)
        verify(!sudoWarning().opened,
               "the dialog was skipped for a screen reader")
    }

    function test_the_skipped_warning_is_still_carried_by_the_checkbox() {
        // The condition on skipping at all. A screen reader reads this
        // description when the control is reached, which is why the dialog
        // can be left out -- so it has to say what the dialog would have.
        var description = field("passwordlessSudoCheck").Accessible.description

        verify(description.length > 0, "the checkbox carries a description")
        verify(description.indexOf("root") !== -1,
               "it names the privilege being granted")
        verify(description.indexOf("without a password") !== -1,
               "and that no password will be required")
    }
}
