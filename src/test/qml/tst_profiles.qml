/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */
import QtQuick
import QtQuick.Window
import QtTest
import RpiImager

TestCase {
    id: testCase
    name: "CustomisationProfiles"
    when: windowShown
    width: 800
    height: 600
    visible: true

    Component { id: wizardComponent; WizardContainer { anchors.fill: parent } }
    property var wiz: null
    property var dialog: null
    property var previousSettings: ({})
    property bool previousOrgEnabled: false

    function init() {
        previousSettings = ImageWriterSingleton.getSavedCustomisationSettings()
        previousOrgEnabled = ImageWriterSingleton.getBoolSetting("connect_org_enabled")
        wiz = wizardComponent.createObject(testCase)
        verify(wiz)
        wiz.overlayRootRef = testCase
        wiz.customizationSupported = true
        wiz.jumpToStep(wiz.firstCustomizationStep)
        dialog = findChild(wiz, "profilesDialog")
        verify(dialog)
    }

    function cleanup() {
        testCase.height = 600
        if (dialog) dialog.close()
        if (wiz) wiz.destroy()
        var profiles = ImageWriterSingleton.customisationProfiles()
        for (var i = 0; i < profiles.length; ++i) {
            if (profiles[i].name.indexOf("QML profile ") === 0)
                ImageWriterSingleton.deleteCustomisationProfile(profiles[i].id)
        }
        ImageWriterSingleton.setSavedCustomisationSettings(previousSettings)
        ImageWriterSingleton.setSetting("connect_org_enabled", previousOrgEnabled)
        wiz = null
        dialog = null
    }

    function control(name) {
        var item = findChild(dialog, name)
        verify(item, name + " exists")
        return item
    }

    function click(name) { mouseClick(control(name)) }

    function test_profiles_controls_open_dialog_data() {
        return [{tag: "load", saving: false}, {tag: "save", saving: true}]
    }

    function test_profiles_controls_open_dialog(data) {
        wiz.hostnameConfigured = true
        wiz.customizationSettings = {hostname: "test-pi"}
        wiz.jumpToStep(data.saving ? wiz.stepWriting : wiz.firstCustomizationStep)
        var button = findChild(wiz, data.saving ? "saveProfilesButton" : "loadProfilesButton")
        verify(button)
        mouseClick(button)
        tryCompare(dialog, "opened", true)
        compare(dialog.saving, data.saving)
    }

    function test_create_rename_duplicate_update_delete() {
        wiz.customizationSettings = {hostname: "first-pi", wifiMode: "open", wifiSSID: "Guest"}
        wiz.openProfiles(true)
        tryCompare(dialog, "opened", true)
        var name = control("profileNameField")
        name.text = "QML profile first"
        click("saveNewProfileButton")
        compare(dialog.profiles.length, 1)
        var originalId = dialog.selectedId
        compare(ImageWriterSingleton.customisationProfile(originalId).settings.hostname, "first-pi")

        name.text = "QML profile renamed"
        click("renameProfileButton")
        compare(dialog.selectedId, originalId)
        compare(ImageWriterSingleton.customisationProfile(originalId).name, "QML profile renamed")

        name.text = "QML profile copy"
        click("duplicateProfileButton")
        compare(dialog.profiles.length, 2)
        var copyId = dialog.selectedId
        verify(copyId !== originalId)
        compare(ImageWriterSingleton.customisationProfile(copyId).settings.wifiMode, "open")
        // Managing a different preset must not change the active configuration.
        compare(wiz.activeProfileId, originalId)

        wiz.customizationSettings = {hostname: "edited-pi"}
        click("updateProfileButton")
        compare(ImageWriterSingleton.customisationProfile(copyId).settings.hostname, "edited-pi")
        compare(ImageWriterSingleton.customisationProfile(originalId).settings.hostname, "first-pi")
        click("deleteProfileButton")
        compare(dialog.profiles.length, 2)
        verify(dialog.confirmingDelete)
        click("deleteProfileButton")
        compare(dialog.profiles.length, 1)
        compare(ImageWriterSingleton.customisationProfile(copyId).name, undefined)
    }

    function test_loading_replaces_previous_project_and_requires_review() {
        verify(ImageWriterSingleton.saveCustomisationProfile("", "QML profile load", {hostname: "loaded-pi", enableI2C: true, enableUsbGadget: true}))
        var id = ImageWriterSingleton.customisationProfiles()[0].id
        wiz.customizationSettings = {hostname: "old-pi", wifiSSID: "Old network", passwordlessSudo: true,
                                     secureBootEnabled: true, piConnectEnabled: true}
        wiz.wifiConfigured = true
        wiz.secureBootEnabled = true
        wiz.piConnectEnabled = true
        wiz.writeAnotherMode = true
        wiz.ccRpiAvailable = false
        wiz.ifAndFeaturesAvailable = false
        ImageWriterSingleton.setSetting("connect_org_enabled", true)
        ImageWriterSingleton.overwriteConnectToken("old-token")
        wiz.openProfiles(false)
        tryCompare(dialog, "opened", true)
        click("loadProfileButton")
        tryCompare(dialog, "opened", false)
        compare(wiz.currentStep, wiz.firstCustomizationStep)
        compare(wiz.activeProfileId, id)
        compare(wiz.customizationSettings.hostname, "loaded-pi")
        compare(wiz.customizationSettings.wifiSSID, undefined)
        compare(wiz.customizationSettings.passwordlessSudo, undefined)
        compare(wiz.customizationSettings.enableI2C, undefined)
        compare(wiz.customizationSettings.enableUsbGadget, undefined)
        compare(ImageWriterSingleton.customisationProfile(id).settings.enableI2C, true)
        compare(wiz.wifiConfigured, false)
        compare(wiz.secureBootEnabled, false)
        compare(wiz.piConnectEnabled, false)
        compare(wiz.writeAnotherMode, false)
        compare(ImageWriterSingleton.getRuntimeConnectToken(), "")
        compare(ImageWriterSingleton.getBoolSetting("connect_org_enabled"), false)
        compare(ImageWriterSingleton.getSavedCustomisationSettings().hostname, "loaded-pi")
        compare(findChild(wiz, "hostnameField").text, "loaded-pi")
        verify(!wiz.isStepPermissible(wiz.stepWriting))
    }

    function test_profile_names_are_displayed_as_plain_text() {
        verify(ImageWriterSingleton.saveCustomisationProfile("", "QML profile <b>literal</b>", {}))
        wiz.openProfiles(false)
        tryCompare(dialog, "opened", true)
        compare(control("profileList").contentItem.textFormat, Text.PlainText)
        compare(control("profileList").currentText, "QML profile <b>literal</b>")
    }

    function test_profile_values_use_the_existing_form_validation() {
        verify(ImageWriterSingleton.saveCustomisationProfile("", "QML profile invalid", {hostname: "bad hostname"}))
        var id = ImageWriterSingleton.customisationProfiles()[0].id
        verify(wiz.loadProfile(id))
        compare(findChild(wiz, "hostnameField").text, "")
        compare(wiz.hostnameConfigured, false)
        verify(findChild(wiz, "hostnameRejectedNotice").visible)
    }

    function test_duplicate_generates_unique_names() {
        wiz.customizationSettings = {hostname: "copy-me"}
        wiz.openProfiles(true)
        tryCompare(dialog, "opened", true)
        control("profileNameField").text = "QML profile original"
        click("saveNewProfileButton")
        var id = dialog.selectedId
        click("duplicateProfileButton")
        compare(dialog.profiles.length, 2)
        verify(dialog.selectedId !== id)
        verify(ImageWriterSingleton.customisationProfile(dialog.selectedId).name !== "QML profile original")
        compare(wiz.activeProfileId, id)
    }

    function test_empty_names_duplicates_and_escape() {
        wiz.openProfiles(true)
        tryCompare(dialog, "opened", true)
        verify(!control("saveNewProfileButton").enabled)
        control("profileNameField").text = "QML profile unique"
        click("saveNewProfileButton")
        var id = dialog.selectedId
        click("saveNewProfileButton")
        verify(dialog.errorMessage.length > 0)
        compare(dialog.profiles.length, 1)
        compare(dialog.selectedId, id)
        control("profileNameField").forceActiveFocus()
        keyClick(Qt.Key_Escape)
        tryCompare(dialog, "opened", false)
        compare(ImageWriterSingleton.customisationProfiles().length, 1)
    }

    function test_profile_dialog_fits_window_and_keyboard_focus_data() {
        return [{tag: "normal", height: 600}, {tag: "minimum", height: 420}]
    }

    function test_profile_dialog_fits_window_and_keyboard_focus(data) {
        testCase.height = data.height
        wiz.openProfiles(true)
        tryCompare(dialog, "opened", true)
        verify(dialog.x >= 0)
        verify(dialog.y >= 0)
        verify(dialog.x + dialog.width <= testCase.width)
        verify(dialog.y + dialog.height <= testCase.height)
        for (var i = 0; i < 2; ++i) {
            var button = control(i === 0 ? "closeProfilesButton" : "loadProfileButton")
            var position = button.mapToItem(testCase, 0, 0)
            verify(position.y >= 0)
            verify(position.y + button.height <= testCase.height, "footer buttons fit inside the window")
        }
        control("profileNameField").forceActiveFocus()
        keyClick(Qt.Key_Tab)
        verify(!control("profileNameField").activeFocus)

    }
}
