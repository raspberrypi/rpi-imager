/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    // Only show and enable this step if OS supports secure boot
    visible: wizardContainer.secureBootAvailable
    enabled: wizardContainer.secureBootAvailable
    
    title: qsTr("Customisation: Secure Boot")
    subtitle: qsTr("Configure secure boot image signing")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save secure boot settings and continue to next customisation step")
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
            // Enable/disable Secure Boot option pill
            ImOptionPill {
                id: secureBootEnablePill
                Layout.fillWidth: true
                text: qsTr("Enable Secure Boot Signing")
                accessibleDescription: qsTr("Sign the boot partition with your RSA key to enable secure boot verification on Raspberry Pi")
                helpLabel: imageWriter.isEmbeddedMode() ? "" : qsTr("Learn about Secure Boot")
                helpUrl: imageWriter.isEmbeddedMode() ? "" : "https://github.com/raspberrypi/usbboot/blob/master/secure-boot-recovery/README.md"
                checked: false
                onToggled: function(isChecked) { 
                    wizardContainer.secureBootEnabled = isChecked
                    // Rebuild focus order when pill state changes
                    root.rebuildFocusOrder()
                }
            }

            // Info text when enabled
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                visible: secureBootEnablePill.checked
                
            Text {
                Layout.fillWidth: true
                text: qsTr("Your boot partition will be signed using the RSA private key configured in App Options.")
                font.family: Style.fontFamily
                font.pixelSize: Style.fontSizeDescription
                color: Style.textDescriptionColor
                wrapMode: Text.WordWrap
            }
            
            Text {
                Layout.fillWidth: true
                text: qsTr("This will create boot.img and boot.sig files required for Raspberry Pi Secure Boot.")
                font.family: Style.fontFamily
                font.pixelSize: Style.fontSizeDescription
                color: Style.textDescriptionColor
                wrapMode: Text.WordWrap
            }
                
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Style.spacingSmall
                    spacing: Style.spacingXSmall
                    
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Private Key: %1").arg(root.getRsaKeyPath() || qsTr("(not configured)"))
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fontSizeCaption
                        color: Style.textDescriptionColor
                        elide: Text.ElideMiddle
                    }
                    
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Public Key Fingerprint: %1").arg(root.getRsaKeyFingerprint() || qsTr("(unavailable)"))
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fontSizeCaption
                        color: Style.textDescriptionColor
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
    ]

    // Helper function to get the RSA key path
    function getRsaKeyPath() {
        if (imageWriter) {
            var keyPath = imageWriter.getStringSetting("secureboot_rsa_key")
            return keyPath || ""
        }
        return ""
    }
    
    // Helper function to get the RSA key fingerprint
    function getRsaKeyFingerprint() {
        if (imageWriter) {
            var keyPath = getRsaKeyPath()
            if (keyPath) {
                var fingerprint = imageWriter.getRsaKeyFingerprint(keyPath)
                return fingerprint || qsTr("(unable to compute)")
            }
        }
        return ""
    }

    Component.onCompleted: {
        root.registerFocusGroup("secureboot_controller", function(){ 
            var items = [secureBootEnablePill.focusItem]
            // Include help link if visible
            if (secureBootEnablePill.helpLinkItem && secureBootEnablePill.helpLinkItem.visible)
                items.push(secureBootEnablePill.helpLinkItem)
            return items
        }, 0)
        
        // Prefill from conserved customization settings
        var settings = wizardContainer.customizationSettings
        if (settings.secureBootEnabled === true || settings.secureBootEnabled === "true") {
            secureBootEnablePill.checked = true
        }
        
        // Ensure focus order is built after initial state
        root.rebuildFocusOrder()
    }

    Connections {
        target: secureBootEnablePill
        function onToggled() { root.rebuildFocusOrder() }
    }
    
    // Save settings when moving to next step
    onNextClicked: {
        // Update conserved customization settings (runtime state)
        if (secureBootEnablePill.checked) {
            wizardContainer.customizationSettings.secureBootEnabled = true
            wizardContainer.secureBootEnabled = true
        } else {
            // User disabled Secure Boot -> remove setting
            delete wizardContainer.customizationSettings.secureBootEnabled
            wizardContainer.secureBootEnabled = false
        }
        
        // Also persist for future sessions
        var saved = imageWriter.getSavedCustomisationSettings()
        if (secureBootEnablePill.checked) {
            saved.secureBootEnabled = true
        } else {
            delete saved.secureBootEnabled
        }
        imageWriter.setSavedCustomisationSettings(saved)
        
        console.log("Secure Boot enabled:", secureBootEnablePill.checked)
    }
    
    // Handle skip button
    onSkipClicked: {
        // Clear all customization flags
        wizardContainer.hostnameConfigured = false
        wizardContainer.localeConfigured = false
        wizardContainer.userConfigured = false
        wizardContainer.wifiConfigured = false
        wizardContainer.sshEnabled = false
        wizardContainer.secureBootEnabled = false
        
        // Jump to writing step
        wizardContainer.jumpToStep(wizardContainer.stepWriting)
    }
}

