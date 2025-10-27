/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Layouts
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    property bool hasSavedUserPassword: false
    property string savedUsername: ""
    
    title: qsTr("Customisation: Choose username")
    subtitle: qsTr("Create a user account for your Raspberry Pi")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save user account settings and continue to next customisation step")
    backButtonAccessibleDescription: qsTr("Return to previous step")
    skipButtonAccessibleDescription: qsTr("Skip all customisation and proceed directly to writing the image")
    
    // Content
    content: [
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Style.sectionPadding
        spacing: Style.stepContentSpacing
        
        WizardSectionContainer {
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.formRowSpacing
                
                WizardFormLabel {
                    id: labelUsername
                    text: qsTr("Username:")
                    Accessible.ignored: false
                    Accessible.focusable: true
                    Accessible.description: qsTr("Enter a username for your Raspberry Pi account. The username must be lowercase and contain only letters, numbers, underscores, and hyphens.")
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }
                
                ImTextField {
                    id: fieldUsername
                    Layout.fillWidth: true
                    placeholderText: qsTr("Enter your username")
                    font.pixelSize: Style.fontSizeInput
                    
                    validator: RegularExpressionValidator {
                        regularExpression: /^[a-z_][a-z0-9_-]*$/
                    }
                }
                
                WizardFormLabel {
                    id: labelPassword
                    text: CommonStrings.password
                    Accessible.ignored: false
                    Accessible.focusable: true
                    Accessible.description: root.hasSavedUserPassword ? qsTr("Enter a new password for this account, or leave blank to keep the previously saved password.") : qsTr("Enter a password for this account. You will need to re-enter it in the next field to confirm.")
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }
                
                ImTextField {
                    id: fieldPassword
                    Layout.fillWidth: true
                    placeholderText: root.hasSavedUserPassword ? qsTr("Saved (hidden) — leave blank to keep") : qsTr("Enter password")
                    echoMode: TextInput.Password
                    font.pixelSize: Style.fontSizeInput
                }
                
                WizardFormLabel {
                    id: labelPasswordConfirm
                    text: qsTr("Confirm password:")
                    Accessible.ignored: false
                    Accessible.focusable: true
                    Accessible.description: root.hasSavedUserPassword ? qsTr("Re-enter the new password to confirm, or leave blank to keep the previously saved password.") : qsTr("Re-enter the password to confirm it matches.")
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }
                
                ImTextField {
                    id: fieldPasswordConfirm
                    Layout.fillWidth: true
                    placeholderText: root.hasSavedUserPassword ? qsTr("Re-enter to change password") : qsTr("Re-enter password")
                    echoMode: TextInput.Password
                    font.pixelSize: Style.fontSizeInput
                }
            }
            
            WizardDescriptionText {
                id: helpText
                text: qsTr("The username must be lowercase and contain only letters, numbers, underscores, and hyphens.")
            }
        }
    }
    ]

    Component.onCompleted: {
        // Include labels before their corresponding fields so users hear the explanation first
        root.registerFocusGroup("user_fields", function(){
            return [labelUsername, fieldUsername, labelPassword, fieldPassword, labelPasswordConfirm, fieldPasswordConfirm, helpText]
        }, 0)
        
        // Set initial focus on the username field
        // Initial focus will automatically go to title, then subtitle, then first control (handled by WizardStepBase)
        
        // Prefill from saved settings (avoid showing raw passwords)
        var saved = imageWriter.getSavedCustomizationSettings()
        if (saved.sshUserName) {
            fieldUsername.text = saved.sshUserName
            root.savedUsername = saved.sshUserName
        }
        if (saved.sshUserPassword) {
            // Indicate a saved (crypted) password exists; do not prefill fields
            root.hasSavedUserPassword = true
        }
    }
    
    // Validation
    // Allow proceed when:
    // - all fields are empty (no customization), or
    // - a new password is entered and matches confirm, or
    // - a saved (crypted) password exists and user leaves password fields blank (with or without username)
    nextButtonEnabled: (
        (fieldUsername.text.length === 0 && fieldPassword.text.length === 0 && fieldPasswordConfirm.text.length === 0)
        || (fieldPassword.text.length > 0 && fieldPasswordConfirm.text.length > 0 && fieldPassword.text === fieldPasswordConfirm.text)
        || (root.hasSavedUserPassword && fieldPassword.text.length === 0 && fieldPasswordConfirm.text.length === 0)
    )
    
    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        var usernameText = fieldUsername.text ? fieldUsername.text.trim() : ""
        var hasPasswords = fieldPassword.text.length > 0 && fieldPassword.text === fieldPasswordConfirm.text
        if (usernameText.length > 0 && hasPasswords) {
            // User entered both username and new password
            saved.sshUserName = usernameText
            // Store crypted password similar to legacy flow
            saved.sshUserPassword = imageWriter.crypt(fieldPassword.text)
            wizardContainer.userConfigured = true
        } else if (usernameText.length > 0 && root.hasSavedUserPassword && fieldPassword.text.length === 0) {
            // User has a username (entered or kept from saved) and saved password (keeping it)
            saved.sshUserName = usernameText
            // Keep existing saved.sshUserPassword
            wizardContainer.userConfigured = true
        } else if (usernameText.length === 0 && fieldPassword.text.length === 0 && fieldPasswordConfirm.text.length === 0) {
            // User cleared all fields -> remove from persisted settings (allows opting out)
            delete saved.sshUserName
            delete saved.sshUserPassword
            wizardContainer.userConfigured = false
        } else {
            // Partial/invalid -> do not change persisted settings, but do not mark configured
            wizardContainer.userConfigured = false
        }
        imageWriter.setSavedCustomizationSettings(saved)
        // Do not log sensitive data
    }
    
    // Handle skip button
    onSkipClicked: {
        // Clear all customization flags
        wizardContainer.hostnameConfigured = false
        wizardContainer.localeConfigured = false
        wizardContainer.userConfigured = false
        wizardContainer.wifiConfigured = false
        wizardContainer.sshEnabled = false
        
        // Jump to writing step
        wizardContainer.jumpToStep(wizardContainer.stepWriting)
    }
} 
