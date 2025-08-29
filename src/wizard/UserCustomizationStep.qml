/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    property bool hasSavedUserPassword: false
    
    title: qsTr("Customization: Choose username")
    subtitle: qsTr("Create a user account for your Raspberry Pi.")
    showSkipButton: true
    
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
                    text: qsTr("Username:")
                }
                
                ImTextField {
                    id: fieldUsername
                    Layout.fillWidth: true
                    placeholderText: qsTr("pi")
                    font.pixelSize: Style.fontSizeInput
                    
                    validator: RegularExpressionValidator {
                        regularExpression: /^[a-z_][a-z0-9_-]*$/
                    }
                }
                
                WizardFormLabel {
                    text: qsTr("Password:")
                }
                
                ImTextField {
                    id: fieldPassword
                    Layout.fillWidth: true
                    placeholderText: root.hasSavedUserPassword ? qsTr("Saved (hidden) — leave blank to keep") : qsTr("Enter password")
                    echoMode: TextInput.Password
                    font.pixelSize: Style.fontSizeInput
                }
                
                WizardFormLabel {
                    text: qsTr("Confirm password:")
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
                text: qsTr("The username must be lowercase and contain only letters, numbers, underscores, and hyphens.")
            }
        }
    }
    ]

    Component.onCompleted: {
        root.registerFocusGroup("user_fields", function(){
            return [fieldUsername, fieldPassword, fieldPasswordConfirm]
        }, 0)
        
        // Set initial focus on the username field
        root.initialFocusItem = fieldUsername
        
        // Prefill from saved settings (avoid showing raw passwords)
        var saved = imageWriter.getSavedCustomizationSettings()
        if (saved.sshUserName) {
            fieldUsername.text = saved.sshUserName
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
    // - a saved (crypted) password exists and user leaves password fields blank
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
        var defaultUsername = "pi"
        var hasPasswords = fieldPassword.text.length > 0 && fieldPasswordConfirm.text.length > 0 && fieldPassword.text === fieldPasswordConfirm.text
        if (usernameText.length > 0 && usernameText !== defaultUsername && hasPasswords) {
            saved.sshUserName = usernameText
            // Store crypted password similar to legacy flow
            saved.sshUserPassword = imageWriter.crypt(fieldPassword.text)
            wizardContainer.userConfigured = true
        } else if (usernameText.length === 0 && fieldPassword.text.length === 0 && fieldPasswordConfirm.text.length === 0) {
            // User cleared fields -> remove from persisted settings
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