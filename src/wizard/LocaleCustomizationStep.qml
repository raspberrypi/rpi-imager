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
    subtitle: qsTr("Configure locale settings")
    showSkipButton: true
    
    // Initialize the component
    Component.onCompleted: {
        // Load timezone and keyboard layout data
        comboTimezone.model = imageWriter.getTimezoneList()
        comboKeyboard.model = imageWriter.getKeymapLayoutList()
        
        // Set default timezone
        var currentTimezone = imageWriter.getTimezone()
        var tzIndex = comboTimezone.find(currentTimezone)
        if (tzIndex !== -1) {
            comboTimezone.currentIndex = tzIndex
        } else {
            comboTimezone.editText = currentTimezone
        }
        
        // Set default keyboard layout
        // Default to "gb" for UK timezone, "us" for everyone else
        var defaultKeyboard = (currentTimezone === "Europe/London") ? "gb" : "us"
        var kbIndex = comboKeyboard.find(defaultKeyboard)
        if (kbIndex !== -1) {
            comboKeyboard.currentIndex = kbIndex
        } else {
            comboKeyboard.editText = defaultKeyboard
        }
    }
    
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
                    id: chkTimezone
                    text: qsTr("Set timezone")
                    checked: true
                }
                
                ComboBox {
                    id: comboTimezone
                    Layout.fillWidth: true
                    enabled: chkTimezone.checked
                    editable: true
                    selectTextByMouse: true
                    font.pixelSize: Style.fontSizeInput
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                ImCheckBox {
                    id: chkKeyboard
                    text: qsTr("Set keyboard layout")
                    checked: true
                }
                
                ComboBox {
                    id: comboKeyboard
                    Layout.fillWidth: true
                    enabled: chkKeyboard.checked
                    editable: true
                    selectTextByMouse: true
                    font.pixelSize: Style.fontSizeInput
                }
            }
            
            WizardDescriptionText {
                text: qsTr("Configure timezone and keyboard layout for your Raspberry Pi.")
            }
        }
    }
    
    // Save settings when moving to next step
    onNextClicked: {
        var settings = {}
        
        if (chkTimezone.checked && comboTimezone.editText) {
            settings.timezone = comboTimezone.editText
            wizardContainer.localeConfigured = true
        } else {
            wizardContainer.localeConfigured = false
        }
        
        if (chkKeyboard.checked && comboKeyboard.editText) {
            settings.keyboard = comboKeyboard.editText
            wizardContainer.localeConfigured = true
        }
        
        // Store settings temporarily
        console.log("Locale settings:", JSON.stringify(settings))
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