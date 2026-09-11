/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
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


    // ── Reaching the authentication choice by keyboard ────────────────
    //
    // The password-or-key radios are only in the step's focus group while
    // SSH is enabled, and the tab order is built once from those groups. So
    // something has to rebuild it when the switch moves -- a handler on the
    // pill's toggled signal, which was uncovered.

    // Everything the tab order reaches, walked from the step's own ring.
    function tabRing() {
        var seen = []
        var cur = step.getNextFocusableElement(null)
        for (var i = 0; i < 30 && cur && seen.indexOf(cur) === -1; i++) {
            seen.push(cur)
            cur = step.getNextFocusableElement(cur)
        }
        return seen
    }

    function test_turning_ssh_on_puts_the_choice_in_the_tab_order() {
        var pill = child("sshEnableToggle")
        verify(!pill.checked, "SSH starts off")
        var before = tabRing()
        verify(before.indexOf(child("sshPasswordAuthRadio")) === -1,
               "the choice is not offered while SSH is off")

        // Clicked, not assigned: Qt reserves toggled() for a person moving
        // the switch, so setting checked would leave the handler unrun --
        // which is the whole thing under test.
        mouseClick(pill.focusItem)
        tryVerify(function () { return pill.checked }, 3000, "SSH is on")
        waitForRendering(step)

        var after = tabRing()
        verify(after.indexOf(child("sshPasswordAuthRadio")) !== -1,
               "the password option is reachable")
        verify(after.indexOf(child("sshPublicKeyAuthRadio")) !== -1,
               "and so is the key option")
    }

    function test_turning_ssh_back_off_takes_the_choice_out_again() {
        // The other direction: tabbing onto a control that is no longer on
        // the screen puts the focus outline nowhere the user can see.
        //
        // Kept out by two things -- the group only returns the radios while
        // SSH is on, and the rebuild skips anything invisible, which they
        // are when the section is hidden. Making the group unconditional
        // fails nothing. What the cases here rest on is the rebuild
        // happening at all, which does fail when removed.
        var pill = child("sshEnableToggle")
        mouseClick(pill.focusItem)
        tryVerify(function () { return pill.checked }, 3000)
        waitForRendering(step)
        verify(tabRing().indexOf(child("sshPasswordAuthRadio")) !== -1)

        mouseClick(pill.focusItem)
        tryVerify(function () { return !pill.checked }, 3000, "SSH is off again")
        waitForRendering(step)

        compare(tabRing().indexOf(child("sshPasswordAuthRadio")), -1,
                "the choice is out of the order with the controls")
    }

    function test_the_switch_itself_stays_reachable_throughout() {
        // Whatever else moves, the way back has to remain in the order --
        // otherwise turning SSH on is one way.
        var pill = child("sshEnableToggle")
        verify(tabRing().indexOf(pill.focusItem) !== -1, "reachable while off")

        mouseClick(pill.focusItem)
        tryVerify(function () { return pill.checked }, 3000)
        waitForRendering(step)

        verify(tabRing().indexOf(pill.focusItem) !== -1, "and while on")
    }

    // ── The key manager's own controls ────────────────────────────────
    //
    // Arriving with keys already saved selects public-key authentication on
    // the user's behalf, so the whole key list is on screen before they have
    // touched anything. None of it was reachable: Show, the Remove button on
    // each key and the add field all live inside SshKeyManager, and
    // WizardStepBase builds its ring from the groups a step registers rather
    // than by walking the tree -- so a nested component that registers
    // nothing contributes nothing.
    //
    // What that leaves is a screen listing keys the user cannot inspect,
    // remove or add to from the keyboard.

    // A step created with a key already in the conserved settings, which is
    // what returning to this page looks like. The keys are read in
    // Component.onCompleted, so they have to be in place before it is built.
    function stepWithSavedKey() {
        if (step) {
            step.destroy()
            step = null
        }
        fakeContainer.customizationSettings = { sshEnabled: true, sshAuthorizedKeys: aKey }
        step = stepComponent.createObject(testCase)
        verify(step, "the step was created")
        waitForRendering(step)
        return step
    }

    function test_a_saved_key_leaves_the_show_button_reachable() {
        stepWithSavedKey()
        compare(keys().keys.length, 1, "the saved key was loaded")
        verify(child("sshPublicKeyAuthRadio").checked,
               "which selects key authentication without being asked")

        verify(tabRing().indexOf(child("sshShowKeysButton")) !== -1,
               "the Show button is in the tab order")
    }

    function test_expanding_the_list_puts_its_controls_in_the_order() {
        stepWithSavedKey()
        var show = child("sshShowKeysButton")
        compare(tabRing().indexOf(child("sshAddKeyField")), -1,
                "nothing from the list is offered while it is collapsed")

        mouseClick(show)
        tryVerify(function () { return keys().expanded }, 3000, "the list opened")
        waitForRendering(step)

        var ring = tabRing()
        verify(ring.indexOf(child("sshRemoveKeyButton")) !== -1,
               "the key can be removed from the keyboard")
        verify(ring.indexOf(child("sshAddKeyField")) !== -1,
               "another key can be typed in")
        verify(ring.indexOf(child("sshAddOrBrowseButton")) !== -1,
               "and one can be browsed for")
    }

    function test_collapsing_the_list_takes_them_out_again() {
        // Tabbing onto a control that is no longer drawn puts the focus
        // outline somewhere the user cannot see it.
        stepWithSavedKey()
        var show = child("sshShowKeysButton")
        mouseClick(show)
        tryVerify(function () { return keys().expanded }, 3000)
        waitForRendering(step)
        verify(tabRing().indexOf(child("sshAddKeyField")) !== -1)

        mouseClick(show)
        tryVerify(function () { return !keys().expanded }, 3000, "the list closed")
        waitForRendering(step)

        compare(tabRing().indexOf(child("sshAddKeyField")), -1,
                "the add field went with it")
    }

    function test_choosing_passwords_takes_the_key_controls_out() {
        // The manager is hidden behind the public-key option, so its
        // controls have to leave the order when that option does.
        stepWithSavedKey()
        verify(tabRing().indexOf(child("sshShowKeysButton")) !== -1)

        mouseClick(child("sshPasswordAuthRadio"))
        tryVerify(function () { return child("sshPasswordAuthRadio").checked }, 3000)
        waitForRendering(step)

        compare(tabRing().indexOf(child("sshShowKeysButton")), -1,
                "Show is out of the order with the list it opens")
    }

    function test_a_key_added_later_can_be_removed_from_the_keyboard() {
        // The Remove buttons come from a Repeater, so they do not exist
        // until there is a key. A ring built once at startup would not have
        // them; one rebuilt when the list changes does.
        var pill = child("sshEnableToggle")
        mouseClick(pill.focusItem)
        tryVerify(function () { return pill.checked }, 3000)
        mouseClick(child("sshPublicKeyAuthRadio"))
        tryVerify(function () { return child("sshPublicKeyAuthRadio").checked }, 3000)
        waitForRendering(step)

        keys().expanded = true
        keys().addKey(aKey)
        tryVerify(function () { return keys().keys.length === 1 }, 3000, "the key went in")
        waitForRendering(step)

        verify(tabRing().indexOf(child("sshRemoveKeyButton")) !== -1,
               "the new key's Remove button joined the order")
    }
}
