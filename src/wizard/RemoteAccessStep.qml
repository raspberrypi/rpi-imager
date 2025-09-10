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
    
    title: qsTr("Customisation: SSH authentication")
    subtitle: qsTr("Configure SSH access.")
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
            // Replace checkbox with an option pill and help link
            ImOptionPill {
                id: sshEnablePill
                Layout.fillWidth: true
                text: qsTr("Enable SSH")
                helpLabel: imageWriter.isEmbeddedMode() ? "" : qsTr("Learn about SSH")
                helpUrl: imageWriter.isEmbeddedMode() ? "" : "https://www.raspberrypi.com/documentation/computers/remote-access.html#ssh"
                checked: false
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.spacingSmallPlus
                enabled: sshEnablePill.checked
                
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
                            // Prefer native file dialog via Imager's wrapper, but only if available
                            if (imageWriter.nativeFileDialogAvailable()) {
                                var home = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
                                var startDir = home && home.length > 0 ? home + "/.ssh" : ""
                                var filters = "Public Key files (*.pub);;All files (*)"
                                var picked = imageWriter.getNativeOpenFileName(qsTr("Select SSH Public Key"), startDir, filters)
                                if (picked && picked.length > 0) {
                                    var contents = imageWriter.readFileContents(picked)
                                    if (contents && contents.length > 0) {
                                        fieldPublicKey.text = contents
                                    } else {
                                        // Failed to read file contents
                                        fieldPublicKey.text = qsTr("Failed to read SSH key file")
                                    }
                                } else {
                                    // User cancelled native dialog; do not auto-open fallback
                                }
                            } else {
                                // Fallback to QML dialog (forced non-native)
                                sshKeyFileDialog.open()
                            }
                        }
                    }
                }
            }
        }
    }
    ]

    Component.onCompleted: {
        root.registerFocusGroup("ssh_controller", function(){ return [sshEnablePill.focusItem] }, 0)
        root.registerFocusGroup("ssh_auth", function(){ return sshEnablePill.checked ? [radioPassword, radioPublicKey] : [] }, 1)
        root.registerFocusGroup("ssh_key", function(){ return sshEnablePill.checked && radioPublicKey.checked ? [fieldPublicKey, browseButton] : [] }, 2)
        // Prefill from saved settings
        var saved = imageWriter.getSavedCustomizationSettings()
        if (saved.sshEnabled === true || saved.sshEnabled === "true") {
            sshEnablePill.checked = true
        }
        if (saved.sshPublicKey) {
            radioPublicKey.checked = true
            radioPassword.checked = false
            fieldPublicKey.text = saved.sshPublicKey
        } else if (saved.sshPasswordAuth === true || saved.sshPasswordAuth === "true") {
            radioPassword.checked = true
            radioPublicKey.checked = false
        }
        // Ensure focus order is built after initial state
        root.rebuildFocusOrder()
    }

    Connections {
        target: sshEnablePill
        function onToggled() { root.rebuildFocusOrder() }
    }
    Connections {
        target: radioPublicKey
        function onCheckedChanged() { root.rebuildFocusOrder() }
    }
    Connections {
        target: radioPassword
        function onCheckedChanged() { root.rebuildFocusOrder() }
    }
    
    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        if (sshEnablePill.checked) {
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
    
    ImFileDialog {
        id: sshKeyFileDialog
        dialogTitle: qsTr("Select SSH Public Key")
        nameFilters: ["*.pub", "*"]
        Component.onCompleted: {
            if (Qt.platform.os === "osx" || Qt.platform.os === "darwin") {
                // Default to ~/.ssh on macOS
                var home = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                var url = "file://" + home + "/.ssh"
                sshKeyFileDialog.currentFolder = url
                sshKeyFileDialog.folder = url
            } else if (Qt.platform.os === "linux") {
                // Default to ~/.ssh on Linux
                var lhome = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                var lurl = "file://" + lhome + "/.ssh"
                sshKeyFileDialog.currentFolder = lurl
                sshKeyFileDialog.folder = lurl
            } else if (Qt.platform.os === "windows") {
                // Default to %USERPROFILE%\.ssh on Windows
                var whome = StandardPaths.writableLocation(StandardPaths.HomeLocation)
                // Use file:/// prefix on Windows
                var wurl = "file:///" + whome + "/.ssh"
                sshKeyFileDialog.currentFolder = wurl
                sshKeyFileDialog.folder = wurl
            }
        }
        onAccepted: {
            if (selectedFile && selectedFile.toString().length > 0) {
                var filePath = selectedFile.toString().replace(/^file:\/\//, "")
                var contents = imageWriter.readFileContents(filePath)
                if (contents && contents.length > 0) {
                    fieldPublicKey.text = contents
                } else {
                    fieldPublicKey.text = qsTr("Failed to read SSH key file")
                }
            }
        }
    }
} 
