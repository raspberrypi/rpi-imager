/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
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
 *
 * The asymmetry is deliberate and worth pinning too: turning the safety net
 * back on takes effect immediately and asks nothing.
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
}
