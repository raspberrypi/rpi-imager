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
    
    title: qsTr("Customisation: Choose locale")
    subtitle: qsTr("Configure timezone and keyboard layout for your Raspberry Pi.")
    showSkipButton: true


    // Initialize the component
    Component.onCompleted: {
        // Load timezone and keyboard layout data
        comboTimezone.model = imageWriter.getTimezoneList()
        comboKeyboard.model = imageWriter.getKeymapLayoutList()
        
        // Prefill from saved settings; fallback to platform defaults
        var saved = imageWriter.getSavedCustomizationSettings()
        var tzToSet = saved.timezone || imageWriter.getTimezone()
        var tzIndex = comboTimezone.find(tzToSet)
        if (tzIndex !== -1) comboTimezone.currentIndex = tzIndex
        else comboTimezone.editText = tzToSet

        var defaultKeyboard = (tzToSet === "Europe/London") ? "gb" : "us"
        var kbToSet = saved.keyboard || defaultKeyboard
        var kbIndex = comboKeyboard.find(kbToSet)
        if (kbIndex !== -1) comboKeyboard.currentIndex = kbIndex
        else comboKeyboard.editText = kbToSet

        // Register focus group for locale controls in proper tab order
        root.registerFocusGroup("locale_controls", function(){ 
            return [comboTimezone, comboKeyboard] 
        }, 0)
        
        // Set initial focus on timezone when entering step
        root.initialFocusItem = comboTimezone
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
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                WizardFormLabel { text: qsTr("Timezone:") }
                ImComboBox {
                    id: comboTimezone
                    Layout.fillWidth: true
                    editable: false
                    selectTextByMouse: true
                    font.pixelSize: Style.fontSizeInput
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                WizardFormLabel { text: qsTr("Keyboard layout:") }
                ImComboBox {
                    id: comboKeyboard
                    Layout.fillWidth: true
                    editable: false
                    selectTextByMouse: true
                    font.pixelSize: Style.fontSizeInput
                    KeyNavigation.tab: root.nextButtonItem
                    KeyNavigation.backtab: comboTimezone
                }
            }
            
        }
    }
    ]
    
    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        var tz = comboTimezone.editText ? comboTimezone.editText.trim() : ""
        var kb = comboKeyboard.editText ? comboKeyboard.editText.trim() : ""
        if (tz.length > 0) saved.timezone = tz; else delete saved.timezone
        if (kb.length > 0) saved.keyboard = kb; else delete saved.keyboard
        wizardContainer.localeConfigured = (tz.length > 0 || kb.length > 0)
        imageWriter.setSavedCustomizationSettings(saved)
        // Avoid logging settings to protect privacy
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
