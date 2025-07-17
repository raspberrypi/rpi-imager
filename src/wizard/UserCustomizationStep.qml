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
    
    title: qsTr("OS Customisation")
    subtitle: qsTr("Configure user account")
    showSkipButton: true
    
    // Content
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Style.sectionPadding
        spacing: Style.stepContentSpacing
        
        WizardSectionContainer {
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                ImCheckBox {
                    id: chkUsername
                    text: qsTr("Create user account")
                    checked: true
                }
                
                Item {
                    Layout.fillWidth: true
                }
            }
            
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.formRowSpacing
                enabled: chkUsername.checked
                
                WizardFormLabel {
                    text: qsTr("Username:")
                }
                
                TextField {
                    id: fieldUsername
                    Layout.fillWidth: true
                    placeholderText: qsTr("pi")
                    font.pixelSize: Style.fontSizeInput
                    
                    Component.onCompleted: {
                        text = "pi"
                    }
                    
                    validator: RegularExpressionValidator {
                        regularExpression: /^[a-z_][a-z0-9_-]*$/
                    }
                }
                
                WizardFormLabel {
                    text: qsTr("Password:")
                }
                
                TextField {
                    id: fieldPassword
                    Layout.fillWidth: true
                    placeholderText: qsTr("Enter password")
                    echoMode: TextInput.Password
                    font.pixelSize: Style.fontSizeInput
                }
                
                WizardFormLabel {
                    text: qsTr("Confirm password:")
                }
                
                TextField {
                    id: fieldPasswordConfirm
                    Layout.fillWidth: true
                    placeholderText: qsTr("Re-enter password")
                    echoMode: TextInput.Password
                    font.pixelSize: Style.fontSizeInput
                }
            }
            
            WizardDescriptionText {
                text: qsTr("Create a user account for your Raspberry Pi. The username must be lowercase and contain only letters, numbers, underscores, and hyphens.")
            }
        }
    }
    
    // Validation
    nextButtonEnabled: !chkUsername.checked || (fieldUsername.text.length > 0 && fieldPassword.text.length > 0 && fieldPassword.text === fieldPasswordConfirm.text)
    
    // Save settings when moving to next step
    onNextClicked: {
        var settings = {}
        
        if (chkUsername.checked && fieldUsername.text.length > 0 && fieldPassword.text.length > 0) {
            settings.username = fieldUsername.text
            settings.password = fieldPassword.text
            wizardContainer.userConfigured = true
        } else {
            wizardContainer.userConfigured = false
        }
        
        // Store settings temporarily
        console.log("User settings:", JSON.stringify(settings))
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