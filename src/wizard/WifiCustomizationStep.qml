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
    
    title: qsTr("OS Customisation")
    subtitle: qsTr("Configure wireless LAN")
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
                    id: chkWifi
                    text: qsTr("Configure wireless LAN")
                    checked: true
                }
                
                Item {
                    Layout.fillWidth: true
                }
            }
            
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.formRowSpacing
                enabled: chkWifi.checked
                
                WizardFormLabel {
                    text: qsTr("SSID:")
                }
                
                TextField {
                    id: fieldWifiSSID
                    Layout.fillWidth: true
                    placeholderText: qsTr("Network name")
                    font.pixelSize: Style.fontSizeInput
                }
                
                WizardFormLabel {
                    text: qsTr("Password:")
                }
                
                TextField {
                    id: fieldWifiPassword
                    Layout.fillWidth: true
                    placeholderText: qsTr("Network password")
                    echoMode: TextInput.Password
                    font.pixelSize: Style.fontSizeInput
                }
                
                WizardFormLabel {
                    text: qsTr("Wireless LAN country:")
                }
                
                ComboBox {
                    id: fieldWifiCountry
                    Layout.fillWidth: true
                    model: ["US", "GB", "DE", "FR"] // Simplified for now
                    font.pixelSize: Style.fontSizeInput
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                ImCheckBox {
                    id: chkWifiHidden
                    text: qsTr("Hidden SSID")
                    enabled: chkWifi.checked
                }
                
                Item {
                    Layout.fillWidth: true
                }
            }
            
            WizardDescriptionText {
                text: qsTr("Configure wireless network settings to connect your Raspberry Pi to WiFi.")
            }
        }
    }
    
    // Save settings when moving to next step
    onNextClicked: {
        var settings = {}
        
        if (chkWifi.checked && fieldWifiSSID.text && fieldWifiPassword.text && fieldWifiCountry.currentText) {
            settings.wifiSSID = fieldWifiSSID.text
            settings.wifiPassword = fieldWifiPassword.text
            settings.wifiCountry = fieldWifiCountry.currentText
            settings.wifiHidden = chkWifiHidden.checked
            wizardContainer.wifiConfigured = true
        } else {
            wizardContainer.wifiConfigured = false
        }
        
        // Store settings temporarily
        console.log("WiFi settings:", JSON.stringify(settings))
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