/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The application options, and which of them survive the dialog closing.
 *
 * Save writes the toggles to persistent settings; Cancel is meant to write
 * nothing. Neither button was covered, so a Cancel that saved anyway -- or a
 * Save that quietly did not -- would have gone unnoticed. Both are the sort of
 * thing a user only discovers later: options they backed out of turning up
 * applied, or options they set having to be set again every launch.
 *
 * Three of the settings carry a rule of their own.
 *
 * disable_warnings is deliberately not persisted, and is not repeated here.
 * tst_app_options already covers it the right way round -- ticking the toggle,
 * confirming, then applying -- and removing the ephemeral-only handling fails
 * that case. Setting the container flag instead, as a first draft of this file
 * did, leaves the checkbox at false and the assertion holds whether or not
 * anything is written, which is no test at all.
 *
 * beep is only stored as enabled when the machine can actually beep, so a
 * setting never claims a capability that is not there. That can only be
 * distinguished on a machine that cannot beep, so the case says so and skips
 * where it would be a tautology.
 *
 * And the organisation API key is kept when its feature flag is toggled off.
 * The comment in the source says so, and it matters: the key is a credential
 * the user pasted in, and a checkbox is not consent to destroy it. Removing it
 * is the Clear action on the Connect step, which has its own tests.
 *
 * Whether it is still there is asked through hasConnectOrgRegistration(),
 * because getStringSetting() refuses that one key by name -- the UI is never
 * allowed to read a stored secret back. That refusal is worth a case of its
 * own, so it is checked here too.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "AppOptionsPersistence"
    when: windowShown
    width: 800
    height: 700
    visible: true

    QtObject {
        id: fakeContainer
        property bool disableWarnings: false
        property bool secureBootAvailable: false
    }

    Component {
        id: dialogComponent
        AppOptionsDialog {
            wizardContainer: fakeContainer
        }
    }

    property var dialog: null

    // Every setting applySettings() writes, so a case can check the lot and
    // say which one moved when it should not have.
    readonly property var boolSettings: ["beep", "eject", "telemetry",
                                         "connect_org_enabled"]

    function clearSettings() {
        for (let i = 0; i < boolSettings.length; i++)
            ImageWriterSingleton.setSetting(boolSettings[i], false)
        ImageWriterSingleton.setSetting("secureboot_rsa_key", "")
        ImageWriterSingleton.setSetting("disable_warnings", false)
    }

    function init() {
        fakeContainer.disableWarnings = false
        clearSettings()
        dialog = dialogComponent.createObject(testCase)
        verify(dialog, "the dialog was created")
        dialog.open()
        tryVerify(function () { return dialog.opened }, 3000, "the dialog opened")
    }

    function cleanup() {
        if (dialog) {
            dialog.close()
            dialog.destroy()
            dialog = null
        }
        clearSettings()
    }

    function child(name) {
        const c = findChild(dialog, name)
        verify(c, "found " + name)
        return c
    }

    function saved(key) {
        return ImageWriterSingleton.getBoolSetting(key)
    }

    // Ticked in code rather than clicked: what is under test is what Save and
    // Cancel do with the state, not how the state got there, and the
    // disable-warnings toggle raises a confirmation on a real click which
    // tst_app_options covers separately.
    function tickEverything() {
        child("ejectToggle").checked = true
        child("telemetryToggle").checked = true
        child("connectOrgToggle").checked = true
    }

    // -- Save writes them ---------------------------------------------------

    function test_saving_writes_the_toggles_to_settings() {
        tickEverything()

        child("optionsSaveButton").clicked()

        // beep is left out: it has a rule of its own, checked below.
        const missing = []
        for (let i = 0; i < boolSettings.length; i++) {
            const key = boolSettings[i]
            if (key !== "beep" && !saved(key))
                missing.push(key)
        }
        compare(missing.length, 0,
                "saving did not write these: " + missing.join(", "))
    }

    function test_saving_writes_the_rsa_key_path() {
        // Not a credential -- the path to one -- but losing it means secure
        // boot silently has no key on the next launch.
        const path = "/home/pi/.ssh/secureboot.pem"
        child("rsaKeyPathField").text = path

        child("optionsSaveButton").clicked()

        compare(ImageWriterSingleton.getStringSetting("secureboot_rsa_key"), path)
    }

    function test_saving_closes_the_dialog() {
        child("optionsSaveButton").clicked()

        tryVerify(function () { return !dialog.visible }, 3000,
                  "Save puts the dialog away as well as applying it")
    }

    // -- Cancel writes nothing ---------------------------------------------

    function test_cancelling_writes_none_of_the_toggles() {
        // The one that would be found out late: options backed out of turning
        // up applied anyway.
        tickEverything()
        child("rsaKeyPathField").text = "/tmp/should-not-be-saved.pem"

        child("optionsCancelButton").clicked()

        const written = []
        for (let i = 0; i < boolSettings.length; i++)
            if (saved(boolSettings[i]))
                written.push(boolSettings[i])
        compare(written.length, 0,
                "cancelling wrote these anyway: " + written.join(", "))
        compare(ImageWriterSingleton.getStringSetting("secureboot_rsa_key"), "",
                "and the key path with them")
    }

    function test_cancelling_closes_the_dialog() {
        child("optionsCancelButton").clicked()

        tryVerify(function () { return !dialog.visible }, 3000)
    }

    // -- The three with rules of their own ---------------------------------

    function test_beep_is_not_stored_as_enabled_where_it_cannot_beep() {
        // A setting claiming a capability the machine does not have would have
        // the application try to beep on every write and fail quietly.
        //
        // Only distinguishable where beeping is unavailable: with a beeper
        // present the guard is `checked && true`, and any assertion holds
        // whether the guard is there or not.
        if (ImageWriterSingleton.isBeepAvailable()) {
            skip("this machine can beep, so the availability guard has nothing "
                 + "to do and the case would pass with it removed")
            return
        }
        child("beepToggle").checked = true

        child("optionsSaveButton").clicked()

        verify(!saved("beep"),
               "a machine that cannot beep does not get a beep setting")
    }

    function test_turning_the_organisation_flag_off_keeps_the_saved_key() {
        // The key is a credential the user pasted in. Unticking a feature flag
        // is not consent to destroy it; the Clear action on the Connect step
        // is.
        ImageWriterSingleton.setConnectOrgRegistration("org-key-kept", "Test org")
        verify(ImageWriterSingleton.hasConnectOrgRegistration(),
               "there is a key to keep")

        child("connectOrgToggle").checked = false
        child("optionsSaveButton").clicked()

        verify(!saved("connect_org_enabled"), "the flag is off")
        verify(ImageWriterSingleton.hasConnectOrgRegistration(),
               "but the key the user pasted in is still there")

        ImageWriterSingleton.clearConnectOrgRegistration()
    }

    function test_turning_the_organisation_flag_back_on_finds_the_key_still_there() {
        ImageWriterSingleton.setConnectOrgRegistration("org-key-kept", "Test org")

        child("connectOrgToggle").checked = true
        child("optionsSaveButton").clicked()

        verify(saved("connect_org_enabled"))
        verify(ImageWriterSingleton.hasConnectOrgRegistration(),
               "so turning the feature off and on again does not cost the "
               + "user their key")

        ImageWriterSingleton.clearConnectOrgRegistration()
    }

    function test_the_saved_key_cannot_be_read_back_through_the_settings() {
        // A stored secret the UI can ask for is a stored secret the UI can
        // echo into a field, a log or a screenshot. getStringSetting refuses
        // this one key by name, and hasConnectOrgRegistration is what the UI
        // is given instead -- enough to say a key is held, not what it is.
        ImageWriterSingleton.setConnectOrgRegistration("org-key-secret", "Test org")
        verify(ImageWriterSingleton.hasConnectOrgRegistration(),
               "the key is stored")

        compare(ImageWriterSingleton.getStringSetting("connect_org_api_key"), "",
                "and cannot be read back out of the settings")

        ImageWriterSingleton.clearConnectOrgRegistration()
    }

    // -- Both buttons are reachable ----------------------------------------

    function test_both_answers_are_reachable_and_described() {
        const save = child("optionsSaveButton")
        const cancel = child("optionsCancelButton")

        verify(save.activeFocusOnTab, "Save is reachable by keyboard")
        verify(cancel.activeFocusOnTab, "and so is Cancel")
        verify(String(save.accessibleDescription).length > 0,
               "and each says what it will do")
        verify(String(cancel.accessibleDescription).length > 0)
    }
}
