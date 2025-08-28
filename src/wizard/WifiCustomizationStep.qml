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
    
    title: qsTr("Customization: Choose WiFi")
    subtitle: qsTr("Configure wireless LAN settings.")
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
        if (saved.wifiCountry) {
            // Set by text; list is populated on completed
            fieldWifiCountry.editText = saved.wifiCountry
        }
        if (saved.wifiHidden !== undefined) {
            chkWifiHidden.checked = !!saved.wifiHidden
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
                    placeholderText: qsTr("Network password")
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
    
    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        var ssid = fieldWifiSSID.text ? fieldWifiSSID.text.trim() : ""
        var country = fieldWifiCountry.currentText || fieldWifiCountry.editText || ""
        var pwd = fieldWifiPassword.text
        var hidden = chkWifiHidden.checked
        if (ssid.length > 0 && country.length > 0) {
            saved.wifiSSID = ssid
            saved.wifiCountry = country
            if (pwd && pwd.length > 0) {
                saved.wifiPassword = pwd
            } else {
                delete saved.wifiPassword
            }
            saved.wifiHidden = hidden
            wizardContainer.wifiConfigured = true
        } else if (ssid.length === 0 && (country.length === 0)) {
            // Cleared -> remove persisted WiFi settings
            delete saved.wifiSSID
            delete saved.wifiPassword
            delete saved.wifiCountry
            delete saved.wifiHidden
            wizardContainer.wifiConfigured = false
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