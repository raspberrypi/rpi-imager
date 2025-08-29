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
    property bool hasSavedWifiPSK: false
    
    title: qsTr("Customization: Choose WiFi")
    subtitle: qsTr("Configure wireless LAN settings.")
    showSkipButton: true
    
    Component.onCompleted: {
        root.registerFocusGroup("wifi_fields", function(){
            return [fieldWifiSSID, fieldWifiPassword, fieldWifiCountry]
        }, 0)
        root.registerFocusGroup("wifi_options", function(){ return [chkWifiHidden] }, 1)
        
        // Set initial focus on SSID
        root.initialFocusItem = fieldWifiSSID
        
        // Prefill from saved settings
        var saved = imageWriter.getSavedCustomizationSettings()
        if (saved.wifiSSID) {
            fieldWifiSSID.text = saved.wifiSSID
        }
        // If not saved, try to auto-detect the current SSID from the system
        if (!fieldWifiSSID.text || fieldWifiSSID.text.length === 0) {
            var detectedSsid = imageWriter.getSSID()
            console.log("WifiCustomizationStep: detected SSID:", detectedSsid)
            if (detectedSsid && detectedSsid.length > 0) {
                fieldWifiSSID.text = detectedSsid
            }
        }
        if (saved.wifiCountry) {
            // Set by text; list is populated on completed
            fieldWifiCountry.editText = saved.wifiCountry
        }
        if (saved.wifiHidden !== undefined) {
            chkWifiHidden.checked = !!saved.wifiHidden
        }
        // Remember if a crypted PSK is already saved (affects placeholder/keep semantics)
        root.hasSavedWifiPSK = !!saved.wifiPasswordCrypt
        // Auto-populate WiFi password from system keychain when available
        // Only when no crypted password is already saved
        if (!root.hasSavedWifiPSK) {
            var psk = imageWriter.getPSK()
            if (psk && psk.length > 0) {
                fieldWifiPassword.text = psk
            }
        }
    }

    // Retry once shortly after load in case permission prompt delayed SSID availability
    Timer {
        interval: 1000
        repeat: false
        running: true
        onTriggered: {
            if (!fieldWifiSSID.text || fieldWifiSSID.text.length === 0) {
                var retrySsid = imageWriter.getSSID()
                console.log("WifiCustomizationStep: retry detected SSID:", retrySsid)
                if (retrySsid && retrySsid.length > 0) {
                    fieldWifiSSID.text = retrySsid
                }
            }
        }
    }

    // Content
    content: [
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Style.sectionPadding
        spacing: Style.stepContentSpacing
        
        WizardSectionContainer {
            // No explicit enable checkbox; intent is inferred from inputs
            
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.formRowSpacing
                
                WizardFormLabel {
                    text: qsTr("SSID:")
                }
                
                ImTextField {
                    id: fieldWifiSSID
                    Layout.fillWidth: true
                    placeholderText: qsTr("Network name")
                    font.pixelSize: Style.fontSizeInput
                }
                
                WizardFormLabel {
                    text: qsTr("Password:")
                }
                
                ImTextField {
                    id: fieldWifiPassword
                    Layout.fillWidth: true
                    placeholderText: root.hasSavedWifiPSK ? qsTr("Saved (hidden) — leave blank to keep") : qsTr("Network password")
                    echoMode: TextInput.Password
                    font.pixelSize: Style.fontSizeInput
                }
                
                WizardFormLabel {
                    text: qsTr("Wireless LAN country:")
                }
                
                ImComboBox {
                    id: fieldWifiCountry
                    Layout.fillWidth: true
                    model: []
                    font.pixelSize: Style.fontSizeInput
                    Component.onCompleted: {
                        model = root.imageWriter.getCountryList()
                        if (model && model.length > 0) {
                            currentIndex = 0
                        }
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                ImCheckBox { id: chkWifiHidden; text: qsTr("Hidden SSID") }
                
                Item {
                    Layout.fillWidth: true
                }
            }
        }
    }
    ]
    
    // Validation: allow proceed when
    // - SSID and country entered and either new PSK provided or a saved crypt exists; or
    // - all WiFi fields are empty (skip)
    nextButtonEnabled: (
        ((fieldWifiSSID.text && fieldWifiSSID.text.trim().length > 0) && (fieldWifiCountry.currentText || fieldWifiCountry.editText))
        ? ((fieldWifiPassword.text && fieldWifiPassword.text.length > 0) || root.hasSavedWifiPSK)
        : true
    )

    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        var ssid = fieldWifiSSID.text ? fieldWifiSSID.text.trim() : ""
        var country = fieldWifiCountry.currentText || fieldWifiCountry.editText || ""
        var pwd = fieldWifiPassword.text
        var hadCrypt = !!saved.wifiPasswordCrypt
        var hidden = chkWifiHidden.checked
        if (ssid.length === 0) {
            // SSID cleared -> remove persisted SSID and password, regardless of country
            delete saved.wifiSSID
            delete saved.wifiPasswordCrypt
            wizardContainer.wifiConfigured = false
        } else if (country.length > 0) {
            saved.wifiSSID = ssid
            saved.wifiCountry = country
            if (pwd && pwd.length > 0) {
                // Persist crypted PSK; avoid storing plaintext
                var isPassphrase = (pwd.length >= 8 && pwd.length < 64)
                saved.wifiPasswordCrypt = isPassphrase ? imageWriter.pbkdf2(pwd, ssid) : pwd
            } else if (!hadCrypt) {
                // No new password entered and none saved earlier -> ensure cleared
                delete saved.wifiPasswordCrypt
            }
            saved.wifiHidden = hidden
            wizardContainer.wifiConfigured = true
        } else {
            // Partial -> do not change persisted settings
            wizardContainer.wifiConfigured = false
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