/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    property bool hasSavedWifiPSK: false
    
    title: qsTr("Customisation: Choose Wi‑Fi")
    subtitle: qsTr("Configure wireless LAN settings")
    showSkipButton: true
    
    // Set initial focus on SSID
    initialFocusItem: fieldWifiSSID

    Component.onCompleted: {
        root.registerFocusGroup("wifi_fields", function(){
            return [fieldWifiSSID, fieldWifiPassword, fieldWifiCountry]
        }, 0)
        root.registerFocusGroup("wifi_options", function(){ return [chkWifiHidden] }, 1)
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
        // Note: WiFi country is initialized by fieldWifiCountry's Component.onCompleted
        // after the model is loaded, which properly handles recommended/saved/default values
        if (saved.wifiHidden !== undefined) {
            chkWifiHidden.checked = saved.wifiHidden === true || saved.wifiHidden === "true"
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
                    text: CommonStrings.password
                }
                
                ImTextField {
                    id: fieldWifiPassword
                    Layout.fillWidth: true
                    placeholderText: root.hasSavedWifiPSK ? qsTr("Saved (hidden) — leave blank to clear") : qsTr("Network password")
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
                    property bool isInitializing: true
                    Component.onCompleted: {
                        model = root.imageWriter.getCountryList()
                        // Always use recommended country from capital city selection
                        // Priority: recommendation from capital city > default (GB)
                        // We intentionally ignore any previously saved wifiCountry
                        var saved = root.imageWriter.getSavedCustomizationSettings()
                        console.log("WifiCustomizationStep: recommendedWifiCountry =", saved.recommendedWifiCountry)
                        if (saved && saved.recommendedWifiCountry && model && model.length > 0) {
                            // Use recommended country from capital city selection
                            var target = saved.recommendedWifiCountry
                            var idx = -1
                            for (var i = 0; i < model.length; i++) {
                                if (model[i] === target) { idx = i; break }
                            }
                            if (idx >= 0) {
                                currentIndex = idx
                            } else {
                                fieldWifiCountry.editText = target
                            }
                        } else if (model && model.length > 0) {
                            // Default to GB if available, else first item
                            var gbIndex = -1
                            for (var j = 0; j < model.length; j++) {
                                if (model[j] === "GB") { gbIndex = j; break }
                            }
                            currentIndex = (gbIndex >= 0) ? gbIndex : 0
                        }
                        isInitializing = false
                        // Now that initialization is complete, update the label once
                        recommendedLabel.updateVisibility()
                    }
                    onCurrentTextChanged: {
                        // Update visibility when selection changes (but not during initialization)
                        if (!isInitializing) {
                            recommendedLabel.updateVisibility()
                        }
                    }
                }
                
                // Empty label to maintain grid alignment
                Item { width: 1; height: 1 }
                
                Text {
                    id: recommendedLabel
                    Layout.fillWidth: true
                    Layout.columnSpan: 1
                    color: "#4CAF50"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    
                    function updateVisibility() {
                        var saved = root.imageWriter.getSavedCustomizationSettings()
                        if (saved && saved.recommendedWifiCountry) {
                            var currentCountry = fieldWifiCountry.currentText || fieldWifiCountry.editText
                            visible = (currentCountry === saved.recommendedWifiCountry)
                            text = visible ? qsTr("✓ Recommended based on your capital city selection") : ""
                            console.log("WifiCustomizationStep: recommendation label visibility =", visible, "current =", currentCountry, "recommended =", saved.recommendedWifiCountry)
                        } else {
                            visible = false
                            text = ""
                            console.log("WifiCustomizationStep: no recommendation available")
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
    
    // WPA2/3 PSK validation helpers
    function isAsciiPrintable(text) {
        for (var i = 0; i < text.length; i++) {
            var code = text.charCodeAt(i)
            if (code < 32 || code > 126) {
                return false
            }
        }
        return true
    }

    function isHex64(text) {
        if (text.length !== 64) {
            return false
        }
        for (var i = 0; i < text.length; i++) {
            var code = text.charCodeAt(i)
            var isDigit = code >= 48 && code <= 57  // 0-9
            var isLower = code >= 97 && code <= 102 // a-f
            var isUpper = code >= 65 && code <= 70  // A-F
            if (!(isDigit || isLower || isUpper)) {
                return false
            }
        }
        return true
    }

    function isValidWifiPassword(text) {
        if (!text || text.length === 0) {
            // Allow open networks
            return true
        }
        if (isHex64(text)) {
            return true
        }
        // 8–63 ASCII printable characters
        return text.length >= 8 && text.length <= 63 && isAsciiPrintable(text)
    }

    // Validation: allow proceed when
    // - SSID and country entered and either new PSK provided or a saved crypt exists; or
    // - all WiFi fields are empty (skip)
    nextButtonEnabled: (
        ((fieldWifiSSID.text && fieldWifiSSID.text.trim().length > 0) && (fieldWifiCountry.currentText || fieldWifiCountry.editText))
        ? root.isValidWifiPassword(fieldWifiPassword.text)
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
        
        // Save country code if provided (even without SSID)
        if (country.length > 0) {
            saved.wifiCountry = country
            wizardContainer.wifiConfigured = true
        } else {
            // No country -> clear country setting
            delete saved.wifiCountry
        }
        
        // Handle SSID and password
        if (ssid.length > 0) {
            saved.wifiSSID = ssid
            if (pwd && pwd.length > 0) {
                // Persist crypted PSK; avoid storing plaintext
                var isPassphrase = (pwd.length >= 8 && pwd.length < 64)
                saved.wifiPasswordCrypt = isPassphrase ? imageWriter.pbkdf2(pwd, ssid) : pwd
            } else {
                // Empty password -> open network; ensure any saved PSK is cleared
                delete saved.wifiPasswordCrypt
            }
            saved.wifiHidden = hidden
            wizardContainer.wifiConfigured = true
        } else {
            // No SSID -> clear SSID and password settings
            delete saved.wifiSSID
            delete saved.wifiPasswordCrypt
            delete saved.wifiHidden
            // If we have a country, still mark as configured
            if (country.length === 0) {
                wizardContainer.wifiConfigured = false
            }
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
