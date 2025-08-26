/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
// import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import QtCore
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("Customization: SSH Authentication")
    subtitle: qsTr("Configure SSH access.")
    showSkipButton: true
    
    // Content
    content: [
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
                    onCheckedChanged: root.requestRecomputeTabOrder()
                }
                
                Item {
                    Layout.fillWidth: true
                }
            }
            
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.spacingSmallPlus
                enabled: chkEnableSSH.checked
                
                WizardFormLabel {
                    text: qsTr("Authentication Mechanism:")
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
                    onCheckedChanged: root.requestRecomputeTabOrder()
                }
                
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingMedium
                    visible: radioPublicKey.checked
                    
                    ImTextField {
                        id: fieldPublicKey
                        Layout.fillWidth: true
                        placeholderText: qsTr("Enter public key or click Browse")
                        font.pixelSize: Style.fontSizeInput
                    }
                    
                    ImButton {
                        id: browseButton
                        text: qsTr("Browse")
                        Layout.minimumWidth: 80
                        onClicked: {
                            sshKeyFileDialog.open()
                        }
                    }
                }
            }
        }
    }
    ]

    Component.onCompleted: {
        root.registerFocusGroup("ssh_controller", function(){ return [chkEnableSSH] }, 0)
        root.registerFocusGroup("ssh_auth", function(){ return chkEnableSSH.checked ? [radioPassword, radioPublicKey] : [] }, 1)
        root.registerFocusGroup("ssh_key", function(){ return chkEnableSSH.checked && radioPublicKey.checked ? [fieldPublicKey, browseButton] : [] }, 2)
        // Prefill from saved settings
        var saved = imageWriter.getSavedCustomizationSettings()
        if (saved.sshEnabled === true || saved.sshEnabled === "true") {
            chkEnableSSH.checked = true
        }
        if (saved.sshPublicKey) {
            radioPublicKey.checked = true
            radioPassword.checked = false
            fieldPublicKey.text = saved.sshPublicKey
        } else if (saved.sshPasswordAuth === true || saved.sshPasswordAuth === "true") {
            radioPassword.checked = true
            radioPublicKey.checked = false
        }
    }
    
    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        if (chkEnableSSH.checked) {
            saved.sshEnabled = true
            saved.sshPasswordAuth = radioPassword.checked
            if (radioPublicKey.checked && fieldPublicKey.text && fieldPublicKey.text.trim().length > 0) {
                saved.sshPublicKey = fieldPublicKey.text.trim()
            } else {
                delete saved.sshPublicKey
            }
            wizardContainer.sshEnabled = true
        } else {
            // User disabled SSH -> remove SSH settings from persistence
            delete saved.sshEnabled
            delete saved.sshPasswordAuth
            delete saved.sshPublicKey
            wizardContainer.sshEnabled = false
        }
        imageWriter.setSavedCustomizationSettings(saved)
        // Do not log remote access settings (may contain sensitive data)
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
        Component.onCompleted: {
            if (Qt.platform.os === "osx" || Qt.platform.os === "darwin") {
                // Default to ~/.ssh on macOS
                var home = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                var url = "file://" + home + "/.ssh"
                if (sshKeyFileDialog.hasOwnProperty("currentFolder")) {
                    sshKeyFileDialog.currentFolder = url
                }
                if (sshKeyFileDialog.hasOwnProperty("folder")) {
                    sshKeyFileDialog.folder = url
                }
            } else if (Qt.platform.os === "linux") {
                // Default to ~/.ssh on Linux
                var lhome = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                var lurl = "file://" + lhome + "/.ssh"
                if (sshKeyFileDialog.hasOwnProperty("currentFolder")) {
                    sshKeyFileDialog.currentFolder = lurl
                }
                if (sshKeyFileDialog.hasOwnProperty("folder")) {
                    sshKeyFileDialog.folder = lurl
                }
            } else if (Qt.platform.os === "windows") {
                // Default to %USERPROFILE%\.ssh on Windows
                var whome = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                // Use file:/// prefix on Windows
                var wurl = "file:///" + whome + "/.ssh"
                if (sshKeyFileDialog.hasOwnProperty("currentFolder")) {
                    sshKeyFileDialog.currentFolder = wurl
                }
                if (sshKeyFileDialog.hasOwnProperty("folder")) {
                    sshKeyFileDialog.folder = wurl
                }
            }
        }
        onAccepted: {
            // Load SSH key file content - simplified for now
            fieldPublicKey.text = qsTr("SSH key loaded from file")
        }
    }
} 