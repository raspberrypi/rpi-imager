/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    subtitle: qsTr("Configure SSH access")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save SSH settings and continue to next customisation step")
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
            // Replace checkbox with an option pill and help link
            ImOptionPill {
                id: sshEnablePill
                Layout.fillWidth: true
                text: qsTr("Enable SSH")
                accessibleDescription: qsTr("Enable secure shell access for remote command-line control of your Raspberry Pi")
                helpLabel: imageWriter.isEmbeddedMode() ? "" : qsTr("Learn about SSH")
                helpUrl: imageWriter.isEmbeddedMode() ? "" : "https://www.raspberrypi.com/documentation/computers/remote-access.html#ssh"
                checked: false
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.spacingSmallPlus
                enabled: sshEnablePill.checked
                
                WizardFormLabel {
                    id: labelAuthMechanism
                    text: qsTr("Authentication mechanism:")
                    Accessible.ignored: false
                    Accessible.focusable: true
                    Accessible.description: qsTr("Choose how you will authenticate when connecting to your Raspberry Pi via SSH. Password authentication uses the account credentials you configured. Public key authentication uses a cryptographic key pair and is more secure.")
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }

                ButtonGroup { id: authGroup }
                
                ImRadioButton {
                    id: radioPassword
                    text: qsTr("Use password authentication")
                    checked: true
                    ButtonGroup.group: authGroup
                    Accessible.description: qsTr("Allow SSH login using the username and password you configured in the previous step.")
                }
                
                ImRadioButton {
                    id: radioPublicKey
                    text: qsTr("Use public key authentication")
                    checked: false
                    ButtonGroup.group: authGroup
                    Accessible.description: qsTr("Allow SSH login using a cryptographic key pair instead of a password. More secure than password authentication.")
                }
                
                WizardFormLabel {
                    id: labelPublicKey
                    text: qsTr("Public key:")
                    visible: radioPublicKey.checked
                    Accessible.ignored: false
                    Accessible.focusable: true
                    Accessible.description: qsTr("Enter or paste your SSH public key, or use the Browse button to select a public key file (typically id_rsa.pub or id_ed25519.pub).")
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }
                
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingMedium
                    visible: radioPublicKey.checked
                    
                    ImTextField {
                        id: fieldPublicKey
                        Layout.fillWidth: true
                        placeholderText: qsTr("Enter public key or click BROWSE")
                        font.pixelSize: Style.fontSizeInput
                    }
                    
                    ImButton {
                        id: browseButton
                        text: CommonStrings.browse
                        accessibleDescription: qsTr("Select an SSH public key file from your computer to enable key-based authentication")
                        Layout.minimumWidth: 80
                        onClicked: {
                            // Prefer native file dialog via Imager's wrapper, but only if available
                            if (imageWriter.nativeFileDialogAvailable()) {
                                var home = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
                                var startDir = home && home.length > 0 ? home + "/.ssh" : ""
                                var picked = imageWriter.getNativeOpenFileName(qsTr("Select SSH Public Key"), startDir, CommonStrings.sshFiltersString)
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
        root.registerFocusGroup("ssh_controller", function(){ 
            var items = [sshEnablePill.focusItem]
            // Include help link if visible
            if (sshEnablePill.helpLinkItem && sshEnablePill.helpLinkItem.visible)
                items.push(sshEnablePill.helpLinkItem)
            return items
        }, 0)
        // Include labels before their corresponding controls so users hear the explanation first
        root.registerFocusGroup("ssh_auth", function(){ return sshEnablePill.checked ? [labelAuthMechanism, radioPassword, radioPublicKey] : [] }, 1)
        root.registerFocusGroup("ssh_key", function(){ return sshEnablePill.checked && radioPublicKey.checked ? [labelPublicKey, fieldPublicKey, browseButton] : [] }, 2)
        // Prefill from conserved customization settings
        var settings = wizardContainer.customizationSettings
        if (settings.sshEnabled === true || settings.sshEnabled === "true") {
            sshEnablePill.checked = true
        }
        if (settings.sshPublicKey) {
            radioPublicKey.checked = true
            radioPassword.checked = false
            fieldPublicKey.text = settings.sshPublicKey
        } else if (settings.sshPasswordAuth === true || settings.sshPasswordAuth === "true") {
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
        target: authGroup
        function onCheckedButtonChanged() {
            root.rebuildFocusOrder()
        }
    }
    
    // Validation: allow proceed when
    // - SSH is not enabled, or
    // - SSH is enabled with password auth, or  
    // - SSH is enabled with public key auth AND a key is provided
    nextButtonEnabled: (
        !sshEnablePill.checked
        || radioPassword.checked
        || (radioPublicKey.checked && fieldPublicKey.text && fieldPublicKey.text.trim().length > 0)
    )
    
    // Save settings when moving to next step
    onNextClicked: {
        // Update conserved customization settings (runtime state)
        if (sshEnablePill.checked) {
            wizardContainer.customizationSettings.sshEnabled = true
            wizardContainer.customizationSettings.sshPasswordAuth = radioPassword.checked
            if (radioPublicKey.checked && fieldPublicKey.text && fieldPublicKey.text.trim().length > 0) {
                wizardContainer.customizationSettings.sshPublicKey = fieldPublicKey.text.trim()
            } else {
                delete wizardContainer.customizationSettings.sshPublicKey
            }
            wizardContainer.sshEnabled = true
        } else {
            // User disabled SSH -> remove SSH settings
            delete wizardContainer.customizationSettings.sshEnabled
            delete wizardContainer.customizationSettings.sshPasswordAuth
            delete wizardContainer.customizationSettings.sshPublicKey
            wizardContainer.sshEnabled = false
        }
        
        // Also persist for future sessions
        var saved = imageWriter.getSavedCustomisationSettings()
        if (sshEnablePill.checked) {
            saved.sshEnabled = true
            saved.sshPasswordAuth = radioPassword.checked
            if (radioPublicKey.checked && fieldPublicKey.text && fieldPublicKey.text.trim().length > 0) {
                saved.sshPublicKey = fieldPublicKey.text.trim()
            } else {
                delete saved.sshPublicKey
            }
        } else {
            delete saved.sshEnabled
            delete saved.sshPasswordAuth
            delete saved.sshPublicKey
        }
        imageWriter.setSavedCustomisationSettings(saved)
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
        imageWriter: root.imageWriter
        parent: root.wizardContainer && root.wizardContainer.overlayRootRef ? root.wizardContainer.overlayRootRef : (root.Window.window ? root.Window.window.overlayRootItem : null)
        anchors.centerIn: parent
        dialogTitle: qsTr("Select SSH Public Key")
        nameFilters: CommonStrings.sshFiltersList
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
