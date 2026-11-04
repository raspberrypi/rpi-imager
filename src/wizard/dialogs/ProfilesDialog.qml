/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../qmlcomponents"
import "../components"
import RpiImager

BaseDialog {
    id: popup
    title: qsTr("Customisation profiles")
    header: null
    implicitWidth: 560
    property var wizardContainer: null
    property bool saving: false
    property var profiles: []
    property string errorMessage: ""
    property string statusMessage: ""
    property bool confirmingDelete: false
    readonly property string selectedId: profileList.currentIndex >= 0 && profileList.currentIndex < profiles.length
                                         ? profiles[profileList.currentIndex].id : ""

    function reveal(item) {
        var top = item.mapToItem(form, 0, 0).y
        var flickable = formFlickable
        if (top < flickable.contentY)
            flickable.contentY = top
        else if (top + item.height > flickable.contentY + formScroll.availableHeight)
            flickable.contentY = Math.max(0, top + item.height - formScroll.availableHeight)
    }

    function refresh(id) {
        profiles = ImageWriterSingleton.customisationProfiles()
        profileList.currentIndex = -1
        for (var i = 0; i < profiles.length; ++i) {
            if (profiles[i].id === id) profileList.currentIndex = i
        }
        if (profileList.currentIndex < 0 && profiles.length > 0)
            profileList.currentIndex = 0
        nameField.text = profileList.currentIndex >= 0 ? profiles[profileList.currentIndex].name : ""
        confirmingDelete = false
        rebuildFocusOrder()
    }

    function save(id, settings, setActive) {
        if (!ImageWriterSingleton.saveCustomisationProfile(id, nameField.value, settings)) {
            errorMessage = qsTr("Use a unique profile name with 1–64 characters. If saving still fails, check that your settings file is writable.")
            return
        }
        var name = nameField.value
        refresh(id)
        for (var i = 0; i < profiles.length; ++i) {
            if (profiles[i].name === name) {
                profileList.currentIndex = i
                nameField.text = profiles[i].name
                if (setActive) wizardContainer.activeProfileId = profiles[i].id
                break
            }
        }
        errorMessage = ""
        statusMessage = qsTr("Profile saved.")
    }

    function duplicateProfile() {
        var profile = ImageWriterSingleton.customisationProfile(selectedId)
        if (nameField.value === profile.name) {
            var base = profile.name.substring(0, 48)
            var candidate = qsTr("%1 (copy)").arg(base)
            var number = 2
            while (profiles.some(function(p) { return p.name.toLowerCase() === candidate.toLowerCase() })) {
                candidate = qsTr("%1 (copy %2)").arg(base).arg(number++)
            }
            nameField.text = candidate
        }
        save("", profile.settings, false)
    }

    function escapePressed() { close() }

    onOpened: {
        statusMessage = ""
        errorMessage = ""
        refresh(wizardContainer.activeProfileId)
    }

    Component.onCompleted: {
        registerFocusGroup("description", function() {
            return ImageWriterSingleton.screenReaderActive ? [heading, description, credentialNotice] : []
        }, -1)
        registerFocusGroup("profiles", function() { return [profileList, nameField] }, 0)
        registerFocusGroup("manage", function() { return [renameButton, duplicateButton, deleteButton] }, 1)
        registerFocusGroup("save", function() { return saving ? [saveNewButton, updateButton] : [] }, 2)
        registerFocusGroup("buttons", function() { return [closeButton, loadButton] }, 3)
    }

    FocusableHeading {
        id: heading
        text: popup.title
        font.pointSize: Style.fontSizeLargeHeading
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
    }

    ScrollView {
        id: formScroll
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(form.implicitHeight,
            Math.max(120, (popup.parent ? popup.parent.height : 700) -
                Style.cardPadding * 2 - heading.implicitHeight - footer.implicitHeight - Style.spacingMedium * 2))
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Flickable {
            id: formFlickable
            contentWidth: width
            contentHeight: form.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: form
                width: formFlickable.width
                spacing: Style.spacingMedium
                WizardDescriptionText {
                    id: description
                    onActiveFocusChanged: if (activeFocus) popup.reveal(description)
                    Layout.fillWidth: true
                    text: popup.saving
                          ? qsTr("Save the settings shown in the write summary for another project or session.")
                          : qsTr("Load a profile, then review its settings for the operating system and Raspberry Pi you selected.")
                }

                WizardFormLabel { text: qsTr("Saved profile:") }
                ImComboBox {
                    id: profileList
                    onActiveFocusChanged: if (activeFocus) popup.reveal(profileList)
                    objectName: "profileList"
                    Layout.fillWidth: true
                    model: popup.profiles
                    textRole: "name"
                    accessiblePurpose: qsTr("Saved profile")
                    enabled: popup.profiles.length > 0
                    onActivated: {
                        nameField.text = popup.profiles[currentIndex].name
                        popup.errorMessage = ""
                        popup.confirmingDelete = false
                    }
                }

                WizardDescriptionText {
                    Layout.fillWidth: true
                    visible: popup.profiles.length === 0
                    text: qsTr("No saved profiles yet. Configure your Raspberry Pi, then save a profile from the write summary.")
                }

                WizardFormLabel { text: qsTr("Profile name:") }
                ImTextField {
                    id: nameField
                    onActiveFocusChanged: if (activeFocus) popup.reveal(nameField)
                    objectName: "profileNameField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("For example, piTV")
                    maximumLength: 64
                    trimWhitespace: true
                    Accessible.name: qsTr("Profile name")
                    onTextEdited: {
                        popup.errorMessage = ""
                        popup.confirmingDelete = false
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingSmall
                    ImButton {
                        id: renameButton
                        onActiveFocusChanged: if (activeFocus) popup.reveal(renameButton)
                        objectName: "renameProfileButton"
                        text: qsTr("Rename")
                        enabled: popup.selectedId !== "" && nameField.value.length > 0
                        onClicked: popup.save(popup.selectedId, ImageWriterSingleton.customisationProfile(popup.selectedId).settings)
                    }
                    ImButton {
                        id: duplicateButton
                        onActiveFocusChanged: if (activeFocus) popup.reveal(duplicateButton)
                        objectName: "duplicateProfileButton"
                        text: qsTr("Duplicate")
                        enabled: popup.selectedId !== "" && nameField.value.length > 0
                        accessibleDescription: qsTr("Create an independent copy of the selected profile")
                        onClicked: popup.duplicateProfile()
                    }
                    Item { Layout.fillWidth: true }
                    ImButton {
                        id: deleteButton
                        onActiveFocusChanged: if (activeFocus) popup.reveal(deleteButton)
                        objectName: "deleteProfileButton"
                        text: popup.confirmingDelete ? qsTr("Confirm delete") : qsTr("Delete")
                        enabled: popup.selectedId !== ""
                        onClicked: {
                            if (!popup.confirmingDelete) {
                                popup.confirmingDelete = true
                                return
                            }
                            var id = popup.selectedId
                            if (!ImageWriterSingleton.deleteCustomisationProfile(id)) {
                                popup.errorMessage = qsTr("The profile could not be deleted. Check that your settings file is writable.")
                                return
                            }
                            if (popup.wizardContainer.activeProfileId === id)
                                popup.wizardContainer.activeProfileId = ""
                            popup.refresh("")
                            popup.errorMessage = ""
                        }
                    }
                }

                RowLayout {
                    visible: popup.saving
                    Layout.fillWidth: true
                    spacing: Style.spacingSmall
                    ImButton {
                        id: saveNewButton
                        onActiveFocusChanged: if (activeFocus) popup.reveal(saveNewButton)
                        objectName: "saveNewProfileButton"
                        text: qsTr("Save new profile")
                        enabled: nameField.value.length > 0
                        onClicked: popup.save("", popup.wizardContainer.customizationSettings, true)
                    }
                    ImButton {
                        id: updateButton
                        onActiveFocusChanged: if (activeFocus) popup.reveal(updateButton)
                        objectName: "updateProfileButton"
                        text: qsTr("Update selected profile")
                        enabled: popup.selectedId !== "" && nameField.value.length > 0
                        accessibleDescription: qsTr("Replace the selected profile with the settings from the write summary")
                        onClicked: popup.save(popup.selectedId, popup.wizardContainer.customizationSettings, true)
                    }
                }

                WizardDescriptionText {
                    id: credentialNotice
                    onActiveFocusChanged: if (activeFocus) popup.reveal(credentialNotice)
                    Layout.fillWidth: true
                    text: qsTr("Profiles store user password hashes and Wi-Fi credentials on this computer. Raspberry Pi Connect, Secure Boot and passwordless sudo must be configured each time.")
                }

                WizardDescriptionText {
                    Layout.fillWidth: true
                    visible: popup.statusMessage.length > 0 && popup.errorMessage.length === 0
                    text: popup.statusMessage
                    Accessible.role: Accessible.AlertMessage
                }

                FocusableText {
                    objectName: "profileError"
                    Layout.fillWidth: true
                    visible: popup.errorMessage.length > 0
                    text: popup.errorMessage
                    color: Style.formLabelErrorColor
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: text
                }

            }
        }
    }

    RowLayout {
        id: footer
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }
        ImButton {
            id: closeButton
            objectName: "closeProfilesButton"
            text: qsTr("Close")
            onClicked: popup.close()
        }
        ImButtonRed {
            id: loadButton
            objectName: "loadProfileButton"
            text: qsTr("Load & review")
            enabled: popup.selectedId !== ""
            onClicked: {
                if (popup.wizardContainer.loadProfile(popup.selectedId))
                    popup.close()
            }
        }
    }
}
