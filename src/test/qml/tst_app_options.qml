/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * AppOptionsDialog: the switch that turns the safety net off.
 *
 * "Disable warnings" suppresses the understanding-confirmations across the
 * whole application -- including the one raised before showing system
 * drives. Three properties keep that from being a one-click mistake, and
 * none of them had ever run in a test: ticking the box does not disable
 * anything by itself, dismissing the confirmation puts the box back, and
 * the setting is never written to disk, so it cannot be inherited by a
 * later session that has forgotten it was ever set.
 */

import QtQuick
import QtQuick.Controls
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "AppOptions"
    when: windowShown
    width: 900
    height: 700
    visible: true

    // The dialog only reads disableWarnings off the container, and writes it
    // straight back; a real WizardContainer would bring every step with it.
    QtObject {
        id: fakeContainer
        property bool disableWarnings: false
        property bool secureBootAvailable: false
        // Clearing the saved settings also has to put the wizard's own
        // "this is configured" flags back, or the steps still claim a
        // hostname and a network that are no longer stored anywhere.
        property var customizationSettings: ({})
        property bool hostnameConfigured: false
        property bool localeConfigured: false
        property bool userConfigured: false
        property bool wifiConfigured: false
        property bool sshEnabled: false
    }

    Component {
        id: dialogComponent
        AppOptionsDialog {
            wizardContainer: fakeContainer
        }
    }

    property var dialog: null

    function init() {
        fakeContainer.disableWarnings = false
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
    }

    function child(name) {
        var c = findChild(dialog, name)
        verify(c, "found " + name)
        return c
    }

    // ── Turning the safety net off ────────────────────────────────────

    function test_opening_the_dialog_raises_nothing() {
        // initialize() writes to the same checkbox the user does, so without
        // its guard flag simply opening the options would raise the
        // confirmation for a setting nobody touched.
        verify(!child("confirmDisableWarningsDialog").opened)
        verify(!fakeContainer.disableWarnings)
    }

    function test_ticking_the_box_asks_first_and_disables_nothing_yet() {
        var toggle = child("disableWarningsToggle")
        toggle.checked = true

        tryVerify(function () { return child("confirmDisableWarningsDialog").opened },
                  3000, "the confirmation was raised")
        verify(!fakeContainer.disableWarnings,
               "warnings are still on until the confirmation is answered")
    }

    function test_dismissing_the_confirmation_puts_the_box_back() {
        var toggle = child("disableWarningsToggle")
        var confirm = child("confirmDisableWarningsDialog")

        toggle.checked = true
        tryVerify(function () { return confirm.opened }, 3000)

        confirm.close()

        tryVerify(function () { return !toggle.checked }, 3000,
                  "the box reverted, so it does not read as disabled")
        verify(!fakeContainer.disableWarnings, "and warnings are still on")
    }

    function test_confirming_is_what_actually_disables_them() {
        var toggle = child("disableWarningsToggle")
        toggle.checked = true
        tryVerify(function () { return child("confirmDisableWarningsDialog").opened }, 3000)

        child("confirmDisableWarningsButton").clicked()

        tryVerify(function () { return fakeContainer.disableWarnings }, 3000,
                  "only the confirm button sets it")
    }

    // ── Turning it back on ────────────────────────────────────────────

    function test_unticking_restores_warnings_without_asking() {
        // The asymmetry is the point: making things safer again should not
        // be gated behind a dialog.
        var toggle = child("disableWarningsToggle")
        toggle.checked = true
        tryVerify(function () { return child("confirmDisableWarningsDialog").opened }, 3000)
        child("confirmDisableWarningsButton").clicked()
        tryVerify(function () { return fakeContainer.disableWarnings }, 3000)

        toggle.checked = false

        tryVerify(function () { return !fakeContainer.disableWarnings }, 3000,
                  "warnings came back")
        verify(!child("confirmDisableWarningsDialog").opened,
               "and nothing was asked on the way")
    }

    // ── It must not outlive the session ───────────────────────────────

    function test_disabling_warnings_is_never_written_to_disk() {
        // If this persisted, someone who turned the safety net off once
        // would never see a confirmation again -- including the one before
        // the system drive list -- with nothing on screen to say why.
        var toggle = child("disableWarningsToggle")
        toggle.checked = true
        tryVerify(function () { return child("confirmDisableWarningsDialog").opened }, 3000)
        child("confirmDisableWarningsButton").clicked()
        tryVerify(function () { return fakeContainer.disableWarnings }, 3000)

        dialog.applySettings()

        verify(!ImageWriterSingleton.getBoolSetting("disable_warnings"),
               "not under disable_warnings")
        verify(!ImageWriterSingleton.getBoolSetting("disableWarnings"),
               "nor under disableWarnings")
    }

    function test_a_fresh_session_starts_with_warnings_on() {
        // What a new run sees: the flag lives on the container, which starts
        // false, and initialize() reads it from there rather than from
        // settings.
        var toggle = child("disableWarningsToggle")
        toggle.checked = true
        tryVerify(function () { return child("confirmDisableWarningsDialog").opened }, 3000)
        child("confirmDisableWarningsButton").clicked()
        tryVerify(function () { return fakeContainer.disableWarnings }, 3000)
        dialog.applySettings()

        // A later session: a container that has never been told otherwise.
        fakeContainer.disableWarnings = false
        dialog.initialize()

        verify(!child("disableWarningsToggle").checked,
               "the box is clear again")
        verify(!child("confirmDisableWarningsDialog").opened,
               "and reloading did not raise the confirmation")
    }


    // ── The buttons that go somewhere, and the one that destroys ──────
    //
    // Four handlers here had never run: the two buttons that lead out of
    // this dialog, the one that permanently removes every saved
    // customisation setting, and the escape key.

    function confirmClear() {
        var d = findChild(dialog, "confirmClearSettingsDialog")
        verify(d, "found the clear-settings confirmation")
        return d
    }

    // Something to lose, on disk and on the container, so that "nothing was
    // cleared" is a statement about the code rather than about an already
    // empty set.
    function savedCount() {
        var m = ImageWriterSingleton.getSavedCustomisationSettings()
        var n = 0
        for (var k in m) n++
        return n
    }

    function stageSomeSettings() {
        ImageWriterSingleton.setSavedCustomisationSettings({
            hostname: "pi-in-the-shed",
            wifiSSID: "Pi Towers"
        })
        verify(savedCount() > 0, "there are settings to lose")
    }

    function test_asking_to_clear_the_saved_settings_confirms_first() {
        stageSomeSettings()

        child("clearSettingsButton").clicked()

        tryVerify(function () { return confirmClear().opened }, 3000,
                  "the confirmation was raised")
        verify(savedCount() > 0, "and nothing has been removed yet")
    }

    function test_cancelling_the_clear_keeps_the_saved_settings() {
        stageSomeSettings()
        child("clearSettingsButton").clicked()
        tryVerify(function () { return confirmClear().opened }, 3000)

        findChild(dialog, "clearSettingsCancelButton").clicked()

        tryVerify(function () { return !confirmClear().visible }, 3000)
        verify(savedCount() > 0,
               "the settings the user backed out of losing are still there")
    }

    function test_escape_on_the_clear_confirmation_keeps_them_too() {
        // Escape is how people dismiss a dialog they did not mean to open.
        stageSomeSettings()
        child("clearSettingsButton").clicked()
        tryVerify(function () { return confirmClear().opened }, 3000)

        confirmClear().escapePressed()

        tryVerify(function () { return !confirmClear().visible }, 3000)
        verify(savedCount() > 0,
               "dismissing the question is not answering yes to it")
    }

    function test_confirming_the_clear_removes_them() {
        // The other direction, without which every case above would pass on
        // a Clear button that did nothing at all.
        stageSomeSettings()
        fakeContainer.hostnameConfigured = true
        fakeContainer.wifiConfigured = true
        child("clearSettingsButton").clicked()
        tryVerify(function () { return confirmClear().opened }, 3000)

        findChild(dialog, "clearSettingsConfirmButton").clicked()

        compare(savedCount(), 0, "the saved settings are gone from disk")
        verify(!fakeContainer.hostnameConfigured,
               "and the wizard no longer claims they are configured")
        verify(!fakeContainer.wifiConfigured)
        tryVerify(function () { return !dialog.visible }, 3000,
                  "and the options dialog closed behind it")
    }

    // ── Escape on the options themselves ──────────────────────────────

    function test_escape_closes_the_options_without_saving() {
        // Same answer as Cancel. Escape being taken as Save would apply
        // options the user was only looking at.
        ImageWriterSingleton.setSetting("eject", false)
        child("ejectToggle").checked = true

        dialog.escapePressed()

        tryVerify(function () { return !dialog.visible }, 3000)
        verify(!ImageWriterSingleton.getBoolSetting("eject"),
               "the toggle the user did not confirm was not written")
    }

    function test_escape_on_the_warning_confirmation_leaves_warnings_on() {
        // The confirmation guarding the switch that turns off every
        // understanding-confirmation in the application.
        var box = child("disableWarningsToggle")
        var confirm = child("confirmDisableWarningsDialog")
        mouseClick(box.focusItem)
        tryVerify(function () { return confirm.opened }, 3000,
                  "the confirmation was raised")

        confirm.escapePressed()

        tryVerify(function () { return !confirm.visible }, 3000)
        verify(!fakeContainer.disableWarnings,
               "the safety net is still up")
        verify(!box.checked, "and the switch went back")
    }

    // ── The way through to the content repository ─────────────────────

    function test_the_repository_button_leads_to_the_repository_dialog() {
        // The only route to it. With this handler gone the button is inert
        // and there is no way to change the source of the OS list at all.
        child("editRepoButton").clicked()

        tryVerify(function () {
            var d = findChild(dialog, "optionsRepoDialog")
            return d && d.opened
        }, 5000, "the repository dialog came up")
        var repo = findChild(dialog, "optionsRepoDialog")
        compare(repo.wizardContainer, fakeContainer,
                "and it has the container, or Apply would throw on it")
        // That comes from the declarative binding on the dialog. The click
        // handler also assigns it when unset, and removing that assignment
        // fails nothing -- redundant rather than load-bearing.
        repo.close()
    }

    // ── The signing key ───────────────────────────────────────────────

    function test_a_chosen_signing_key_is_recorded_as_a_path() {
        // The picker hands back a file:// url and the signer wants a path.
        // Left as a url it is written to the settings and secure boot fails
        // later, on a different screen, with nothing pointing back here.
        var picker = findChild(dialog, "rsaKeyFileDialog")
        verify(picker, "found the key picker")
        child("rsaKeyPathField").text = ""

        picker.selectedFile = "file:///home/pi/.ssh/secureboot.pem"
        picker.accepted()

        compare(child("rsaKeyPathField").text, "/home/pi/.ssh/secureboot.pem")
    }
    function test_the_key_button_opens_the_picker() {
        // How a user reaches the picker the case above drives directly.
        // There is no other way to a signing key from this dialog, so a
        // button that opened nothing would leave the secure-boot option
        // permanently unusable with nothing said about why.
        //
        // Emitted rather than clicked: this sits inside a Popup, and the
        // offscreen harness does not deliver synthesised presses into one.
        // The harness also forces the in-app picker, the way
        // --qml-file-dialogs does, so this runs on a machine with a native
        // dialog as well as one without.
        var picker = findChild(dialog, "rsaKeyFileDialog")
        verify(picker, "found the key picker")

        child("secureBootKeyButton").clicked()

        tryVerify(function () { return picker.opened }, 3000,
                  "the key button opened the picker")

        picker.close()
        tryVerify(function () { return !picker.visible }, 3000)
    }
}
