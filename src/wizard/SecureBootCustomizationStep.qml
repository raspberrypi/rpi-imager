/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCore
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    // Track RSA key path for reactive UI updates
    property string rsaKeyPath: getRsaKeyPath()

    // OTP secure-boot provisioning UI is scaffolded but not yet wired to a backend:
    // the pill below sets state nothing consumes, and ConfirmOtpProgramDialog is never
    // shown. Keep the section hidden until the rpiboot OTP-bootstrap flow is ported from
    // rpi-sb-provisioner; flip this to true (or remove the gate) when it lands. Shipping
    // it inert would present misleading "device is locked to signed boot" UI.
    readonly property bool otpProvisioningImplemented: false

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
            // RSA Key selection button
            ImOptionButton {
                id: rsaKeyButton
                text: qsTr("RSA Private Key")
                btnText: root.rsaKeyPath ? qsTr("Change") : qsTr("Select")
                accessibleDescription: qsTr("Select an RSA 2048-bit private key for signing boot images in secure boot mode")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
                onClicked: {
                    // Prefer native file dialog via Imager's wrapper, but only if available
                    if (ImageWriterSingleton.nativeFileDialogAvailable()) {
                        var home = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
                        var startDir = home && home.length > 0 ? home + "/.ssh" : ""
                        var keyPath = ImageWriterSingleton.getNativeOpenFileName(
                            qsTr("Select RSA Private Key"), 
                            startDir,
                            qsTr("PEM Files (*.pem);;All Files (*)")
                        );
                        if (keyPath) {
                            ImageWriterSingleton.setSetting("secureboot_rsa_key", keyPath)
                            // Update tracked property and wizard container state
                            root.rsaKeyPath = keyPath
                            root.wizardContainer.secureBootKeyConfigured = true
                            // Rebuild focus order and update UI
                            root.rebuildFocusOrder()
                        }
                    } else {
                        // Fallback to QML dialog (forced non-native)
                        rsaKeyFileDialog.open()
                    }
                }
            }
            
            // Show key status if configured
            Text {
                Layout.fillWidth: true
                visible: root.rsaKeyPath && root.rsaKeyPath.length > 0
                text: qsTr("Selected: %1").arg(root.rsaKeyPath)
                font.family: Style.fontFamily
                font.pointSize: Style.fontSizeCaption
                color: Style.textDescriptionColor
                elide: Text.ElideMiddle
            }
            
            // Enable/disable Secure Boot option pill
            ImOptionPill {
                id: secureBootEnablePill
                Layout.fillWidth: true
                Layout.topMargin: Style.spacingMedium
                text: qsTr("Enable Secure Boot Signing")
                accessibleDescription: qsTr("Sign the boot partition with your RSA key to enable secure boot verification on Raspberry Pi")
                helpLabel: ImageWriterSingleton.isEmbeddedMode() ? "" : qsTr("Learn about Secure Boot")
                helpUrl: ImageWriterSingleton.isEmbeddedMode() ? "" : "https://github.com/raspberrypi/usbboot/blob/master/secure-boot-recovery/README.md"
                checked: false
                // Only enable if RSA key is configured
                enabled: root.rsaKeyPath && root.rsaKeyPath.length > 0
                onToggled: function(isChecked) { 
                    root.wizardContainer.secureBootEnabled = isChecked
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
                text: qsTr("Your boot partition will be signed using the selected RSA private key.")
                font.family: Style.fontFamily
                font.pointSize: Style.fontSizeDescription
                color: Style.textDescriptionColor
                wrapMode: Text.WordWrap
            }
            
            Text {
                Layout.fillWidth: true
                text: qsTr("This will create boot.img and boot.sig files required for Raspberry Pi Secure Boot.")
                font.family: Style.fontFamily
                font.pointSize: Style.fontSizeDescription
                color: Style.textDescriptionColor
                wrapMode: Text.WordWrap
            }
                
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Style.spacingSmall
                    spacing: Style.spacingXSmall
                    
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Public Key Fingerprint: %1").arg(root.getRsaKeyFingerprint() || qsTr("(unavailable)"))
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSizeCaption
                        color: Style.textDescriptionColor
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
            
            // Warning if no key is configured but user tries to enable
            Text {
                Layout.fillWidth: true
                Layout.topMargin: Style.spacingSmall
                visible: !root.rsaKeyPath || root.rsaKeyPath.length === 0
                text: qsTr("Please select an RSA private key above to enable secure boot signing.")
                font.family: Style.fontFamily
                font.pointSize: Style.fontSizeDescription
                color: Style.formLabelErrorColor
                wrapMode: Text.WordWrap
            }

            // OTP Provisioning section (only shown when an rpiboot device is selected)
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Style.spacingLarge
                implicitHeight: otpColumn.implicitHeight + Style.spacingMedium * 2
                color: "transparent"
                border.color: Style.titleSeparatorColor
                border.width: 1
                radius: Style.sectionBorderRadius
                visible: root.otpProvisioningImplemented
                         && typeof ImageWriterSingleton.isRpibootDevice === "function" && ImageWriterSingleton.isRpibootDevice()

                ColumnLayout {
                    id: otpColumn
                    anchors.fill: parent
                    anchors.margins: Style.spacingMedium

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("OTP Secure Boot Provisioning")
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSizeSubtitle
                        font.bold: true
                        color: Style.formLabelColor
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSizeDescription
                        color: Style.textDescriptionColor
                        text: qsTr("Program the secure boot public key hash into the device's OTP memory. This is a permanent, irreversible operation.")
                    }

                    ImOptionPill {
                        id: otpProvisionPill
                        Layout.fillWidth: true
                        Layout.topMargin: Style.spacingSmall
                        text: qsTr("Provision Secure Boot OTP")
                        accessibleDescription: qsTr("Permanently program the secure boot key into device OTP memory")
                        checked: false
                        enabled: root.rsaKeyPath && root.rsaKeyPath.length > 0
                        onToggled: function(isChecked) {
                            root.wizardContainer.otpProvisioningEnabled = isChecked
                            root.rebuildFocusOrder()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: otpProvisionPill.checked
                        wrapMode: Text.WordWrap
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSizeCaption
                        color: "#C62828"
                        text: qsTr("WARNING: After OTP provisioning, this device will ONLY boot images signed with the selected key. This cannot be undone.")
                    }
                }
            }
        }
    }
    ]

    // Helper function to get the RSA key path
    function getRsaKeyPath() {
        if (ImageWriterSingleton) {
            var keyPath = ImageWriterSingleton.getStringSetting("secureboot_rsa_key")
            return keyPath || ""
        }
        return ""
    }
    
    // Helper function to get the RSA key fingerprint
    function getRsaKeyFingerprint() {
        if (ImageWriterSingleton && root.rsaKeyPath) {
            var fingerprint = ImageWriterSingleton.getRsaKeyFingerprint(root.rsaKeyPath)
            return fingerprint || qsTr("(unable to compute)")
        }
        return ""
    }

    // File dialog for RSA key selection (fallback when native dialog unavailable)
    ImFileDialog {
        id: rsaKeyFileDialog
        parent: root.parent
        anchors.centerIn: parent
        dialogTitle: qsTr("Select RSA Private Key")
        nameFilters: [qsTr("PEM Files (*.pem)"), qsTr("All Files (*)")]
        Component.onCompleted: {
            // Default to the user's ~/.ssh folder. writableLocation() already returns a
            // platform-correct file:// URL, so just append the subfolder.
            var home = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
            if (home.length > 0) {
                var url = home + "/.ssh"
                rsaKeyFileDialog.currentFolder = url
                rsaKeyFileDialog.folder = url
            }
        }
        onAccepted: {
            if (selectedFile && selectedFile.toString().length > 0) {
                var filePath = selectedFile.toString().replace(/^file:\/\//, "")
                ImageWriterSingleton.setSetting("secureboot_rsa_key", filePath)
                // Update tracked property and wizard container state
                root.rsaKeyPath = filePath
                root.wizardContainer.secureBootKeyConfigured = true
                // Rebuild focus order and update UI
                root.rebuildFocusOrder()
            }
        }
    }

    Component.onCompleted: {
        // Initialize RSA key path from settings
        root.rsaKeyPath = getRsaKeyPath()
        
        root.registerFocusGroup("secureboot_controller", function(){ 
            var items = [rsaKeyButton.focusItem, secureBootEnablePill.focusItem]
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
        var saved = ImageWriterSingleton.getSavedCustomisationSettings()
        if (secureBootEnablePill.checked) {
            saved.secureBootEnabled = true
        } else {
            delete saved.secureBootEnabled
        }
        ImageWriterSingleton.setSavedCustomisationSettings(saved)
        
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

