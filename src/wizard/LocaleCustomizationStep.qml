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
    
    // UI state management
    property string currentMode: "selection" // "selection", "map", "manual"
    property string selectedTimezone: ""
    property string selectedKeyboard: ""
    property string selectedWifiCountry: ""
    property string selectedLanguage: ""
    
    title: qsTr("Customisation: Localisation")
    subtitle: currentMode === "selection" ? qsTr("Choose how to configure your location settings") :
              currentMode === "map" ? qsTr("Click on the map to select your location") :
              qsTr("Configure the time zone and keyboard layout for your Raspberry Pi")
    showSkipButton: currentMode === "selection"
    showBackButton: currentMode !== "selection"

    // Dynamic initial focus based on mode
    initialFocusItem: currentMode === "selection" ? btnSelectByMap :
                     currentMode === "manual" ? comboTimezone : null

    // Initialize the component
    Component.onCompleted: {
        initializeData()
        
        // Register all focus groups once - visibility will control which are active
        root.registerFocusGroup("selection_buttons", function(){ 
            return [btnSelectByMap, btnEnterManually] 
        }, 0)
        
        root.registerFocusGroup("map_controls", function(){ 
            return [worldMapSelector] 
        }, 1)
        
        root.registerFocusGroup("locale_controls", function(){ 
            return [comboTimezone, comboKeyboard] 
        }, 2)
    }
    
    function initializeData() {
        // Load timezone and keyboard layout data for manual mode
        if (comboTimezone.model === undefined) {
            comboTimezone.model = imageWriter.getTimezoneList()
        }
        if (comboKeyboard.model === undefined) {
            comboKeyboard.model = imageWriter.getKeymapLayoutList()
        }
        
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
        
        // Store current selections
        selectedTimezone = tzToSet
        selectedKeyboard = kbToSet
    }
    
    // Content - dynamically switches based on mode
    content: [
        // Mode Selection View
        ColumnLayout {
            visible: currentMode === "selection"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Style.sectionPadding
            spacing: Style.stepContentSpacing
            
            WizardSectionContainer {
                WizardDescriptionText {
                    text: qsTr("Choose how you'd like to configure your location settings:")
                }
                
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingMedium
                    
                    ImButton {
                        id: btnSelectByMap
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        text: qsTr("Select by Map")
                        font.pixelSize: Style.fontSizeButton
                        onClicked: {
                            currentMode = "map"
                        }
                    }
                    
                    ImButton {
                        id: btnEnterManually
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        text: qsTr("Enter Manually")
                        font.pixelSize: Style.fontSizeButton
                        onClicked: {
                            currentMode = "manual"
                        }
                    }
                }
            }
        },
        
        // Map Selection View
        ColumnLayout {
            visible: currentMode === "map"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: Style.sectionPadding
            spacing: Style.stepContentSpacing
            
            WizardSectionContainer {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                WorldMapSelector {
                    id: worldMapSelector
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 400
                    
                    onLocationSelected: function(timezone, keyboardLayout, wifiCountry, language) {
                        root.selectedTimezone = timezone
                        root.selectedKeyboard = keyboardLayout
                        root.selectedWifiCountry = wifiCountry
                        root.selectedLanguage = language
                        
                        // Update manual controls in case user switches back
                        var tzIndex = comboTimezone.find(timezone)
                        if (tzIndex !== -1) comboTimezone.currentIndex = tzIndex
                        else comboTimezone.editText = timezone
                        
                        var kbIndex = comboKeyboard.find(keyboardLayout)
                        if (kbIndex !== -1) comboKeyboard.currentIndex = kbIndex
                        else comboKeyboard.editText = keyboardLayout
                    }
                }
                
                WizardDescriptionText {
                    Layout.topMargin: Style.spacingSmall
                    text: selectedTimezone ? 
                          qsTr("Selected: %1 (%2)").arg(selectedTimezone).arg(selectedKeyboard) :
                          qsTr("Click on the map to select your location, then click Next to continue.")
                }
            }
        },
        
        // Manual Entry View
        ColumnLayout {
            visible: currentMode === "manual"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Style.sectionPadding
            spacing: Style.stepContentSpacing
            
            WizardSectionContainer {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingMedium
                    
                    WizardFormLabel { text: qsTr("Time zone:") }
                    ImComboBox {
                        id: comboTimezone
                        Layout.fillWidth: true
                        editable: false
                        selectTextByMouse: true
                        font.pixelSize: Style.fontSizeInput
                        
                        onCurrentTextChanged: {
                            selectedTimezone = currentText
                        }
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
                        
                        onCurrentTextChanged: {
                            selectedKeyboard = currentText
                        }
                    }
                }
            }
        }
    ]
    
    // Enable next button based on mode and selection
    nextButtonEnabled: currentMode === "selection" ? false :
                      currentMode === "map" ? selectedTimezone.length > 0 :
                      (comboTimezone.currentText.length > 0 && comboKeyboard.currentText.length > 0)
    
    // Handle back button - return to mode selection
    onBackClicked: {
        currentMode = "selection"
    }
    
    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        
        // Use selected values from either map or manual mode
        var tz = selectedTimezone.trim()
        var kb = selectedKeyboard.trim()
        
        if (tz.length > 0) saved.timezone = tz; else delete saved.timezone
        if (kb.length > 0) saved.keyboard = kb; else delete saved.keyboard
        
        // Also save WiFi country if selected from map
        if (currentMode === "map" && selectedWifiCountry.length > 0) {
            saved.wifiCountry = selectedWifiCountry
        }
        
        wizardContainer.localeConfigured = (tz.length > 0 || kb.length > 0)
        imageWriter.setSavedCustomizationSettings(saved)
        // Avoid logging settings to protect privacy
    }
    
    // Handle skip button (only available in selection mode)
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
