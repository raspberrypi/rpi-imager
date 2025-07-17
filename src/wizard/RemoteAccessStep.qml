/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("OS Customisation")
    subtitle: qsTr("Configure remote access")
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
                    id: chkEnableSSH
                    text: qsTr("Enable SSH")
                    checked: false
                }
                
                Item {
                    Layout.fillWidth: true
                }
            }
            
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                enabled: chkEnableSSH.checked
                
                WizardFormLabel {
                    text: qsTr("SSH authentication:")
                }
                
                ImRadioButton {
                    id: radioPassword
                    text: qsTr("Use password authentication")
                    checked: true
                }
                
                ImRadioButton {
                    id: radioPublicKey
                    text: qsTr("Use public key authentication")
                    checked: false
                }
                
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingMedium
                    visible: radioPublicKey.checked
                    
                    TextField {
                        id: fieldPublicKey
                        Layout.fillWidth: true
                        placeholderText: qsTr("Enter public key or click Browse")
                        font.pixelSize: Style.fontSizeInput
                    }
                    
                    ImButton {
                        text: qsTr("Browse")
                        Layout.minimumWidth: 80
                        onClicked: {
                            sshKeyFileDialog.open()
                        }
                    }
                }
            }
            
            WizardDescriptionText {
                text: qsTr("SSH allows you to remotely access your Raspberry Pi from another computer.")
            }
        }
    }
    
    // Save settings when moving to next step
    onNextClicked: {
        var settings = {}
        
        if (chkEnableSSH.checked) {
            settings.sshEnabled = true
            settings.sshPasswordAuth = radioPassword.checked
            if (radioPublicKey.checked && fieldPublicKey.text) {
                settings.sshPublicKey = fieldPublicKey.text
            }
            wizardContainer.sshEnabled = true
        } else {
            wizardContainer.sshEnabled = false
        }
        
        // Store settings temporarily
        console.log("Remote access settings:", JSON.stringify(settings))
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
    
    // File dialog for SSH key selection
    property alias sshKeyFileDialog: sshKeyFileDialog
    
    FileDialog {
        id: sshKeyFileDialog
        title: qsTr("Select SSH Public Key")
        nameFilters: ["Public Key files (*.pub)", "All files (*)"]
        onAccepted: {
            // Load SSH key file content - simplified for now
            fieldPublicKey.text = qsTr("SSH key loaded from file")
        }
    }
} 