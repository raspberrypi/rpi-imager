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
    ScrollView {
        id: sshScroll
        anchors.fill: parent
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Style.sectionPadding
            spacing: Style.spacingSmall  // Reduced spacing to trim whitespace under subtitle
            width: sshScroll.availableWidth
            
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
                    
                    // Compact authentication mechanism: label on left, radio buttons on right
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.spacingMedium
                        
                        WizardFormLabel {
                            id: labelAuthMechanism
                            text: qsTr("Authentication mechanism:")
                            accessibleDescription: qsTr("Choose how you will authenticate when connecting to your Raspberry Pi via SSH. Password authentication uses the account credentials you configured. Public key authentication uses a cryptographic key pair and is more secure.")
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: Style.spacingXXSmall  // Align with first radio button
                        }
                        
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Style.spacingXXSmall
                            
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
                        }
                    }
                    
                    // SSH Key Manager (expands naturally within outer ScrollView)
                    SshKeyManager {
                        id: sshKeyManager
                        Layout.fillWidth: true
                        imageWriter: root.imageWriter
                        visible: radioPublicKey.checked
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
        // Labels are automatically skipped when screen reader is not active (via activeFocusOnTab)
        root.registerFocusGroup("ssh_auth", function(){ return sshEnablePill.checked ? [labelAuthMechanism, radioPassword, radioPublicKey] : [] }, 1)
        // Prefill from conserved customization settings
        var settings = wizardContainer.customizationSettings
        if (settings.sshEnabled === true || settings.sshEnabled === "true") {
            sshEnablePill.checked = true
        }
        
        // Load SSH keys from settings (prefer sshAuthorizedKeys, fallback to sshPublicKey)
        var keysToLoad = []
        if (settings.sshAuthorizedKeys) {
            // Split by newlines
            var lines = settings.sshAuthorizedKeys.split(/\r?\n/)
            for (var i = 0; i < lines.length; i++) {
                var trimmed = lines[i].trim()
                if (trimmed.length > 0) {
                    keysToLoad.push(trimmed)
                }
            }
        } else if (settings.sshPublicKey) {
            // Legacy: single key, split by newlines in case it's multi-line
            var pubLines = settings.sshPublicKey.split(/\r?\n/)
            for (var j = 0; j < pubLines.length; j++) {
                var pubTrimmed = pubLines[j].trim()
                if (pubTrimmed.length > 0) {
                    keysToLoad.push(pubTrimmed)
                }
            }
        }
        
        if (keysToLoad.length > 0) {
            radioPublicKey.checked = true
            radioPassword.checked = false
            sshKeyManager.keys = keysToLoad
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
    // - SSH is enabled with public key auth AND at least one key is provided
    nextButtonEnabled: (
        !sshEnablePill.checked
        || radioPassword.checked
        || (radioPublicKey.checked && sshKeyManager.keys.length > 0)
    )
    
    // Save settings when moving to next step
    onNextClicked: {
        // Update conserved customization settings (runtime state)
        if (sshEnablePill.checked) {
            wizardContainer.customizationSettings.sshEnabled = true
            wizardContainer.customizationSettings.sshPasswordAuth = radioPassword.checked
            if (radioPublicKey.checked && sshKeyManager.keys.length > 0) {
                // Save as sshAuthorizedKeys (multi-line string) for consistency
                wizardContainer.customizationSettings.sshAuthorizedKeys = sshKeyManager.getAllKeysAsString()
                // Remove legacy sshPublicKey if it exists
                delete wizardContainer.customizationSettings.sshPublicKey
            } else {
                delete wizardContainer.customizationSettings.sshAuthorizedKeys
                delete wizardContainer.customizationSettings.sshPublicKey
            }
            wizardContainer.sshEnabled = true
        } else {
            // User disabled SSH -> remove SSH settings
            delete wizardContainer.customizationSettings.sshEnabled
            delete wizardContainer.customizationSettings.sshPasswordAuth
            delete wizardContainer.customizationSettings.sshAuthorizedKeys
            delete wizardContainer.customizationSettings.sshPublicKey
            wizardContainer.sshEnabled = false
        }
        
        // Also persist for future sessions
        var saved = imageWriter.getSavedCustomisationSettings()
        if (sshEnablePill.checked) {
            saved.sshEnabled = true
            saved.sshPasswordAuth = radioPassword.checked
            if (radioPublicKey.checked && sshKeyManager.keys.length > 0) {
                // Save as sshAuthorizedKeys (multi-line string) for consistency
                saved.sshAuthorizedKeys = sshKeyManager.getAllKeysAsString()
                // Remove legacy sshPublicKey if it exists
                delete saved.sshPublicKey
            } else {
                delete saved.sshAuthorizedKeys
                delete saved.sshPublicKey
            }
        } else {
            delete saved.sshEnabled
            delete saved.sshPasswordAuth
            delete saved.sshAuthorizedKeys
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
} 
