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
    
    title: qsTr("Customisation: Localisation")
    subtitle: qsTr("Select your location for suggested time zone and keyboard layout")
    showSkipButton: true

    // Set initial focus on capital city selector when entering step
    initialFocusItem: comboCapitalCity
    
    // Track if user manually changed fields (to avoid overwriting their choices)
    property bool userChangedTimezone: false
    property bool userChangedKeyboard: false

    // Initialize the component
    Component.onCompleted: {
        // Load capital cities, timezones and keyboard layout data
        comboCapitalCity.model = imageWriter.getCapitalCitiesList()
        comboTimezone.model = imageWriter.getTimezoneList()
        comboKeyboard.model = imageWriter.getKeymapLayoutList()
        
        // Prefill from saved settings; fallback to platform defaults
        var saved = imageWriter.getSavedCustomizationSettings()
        
        // Restore saved capital city if available
        if (saved.capitalCity) {
            var cityIndex = comboCapitalCity.find(saved.capitalCity)
            if (cityIndex !== -1) {
                comboCapitalCity.currentIndex = cityIndex
                // Ensure WiFi country recommendation is always up to date
                Qt.callLater(root.onCapitalCityChanged)
            }
        }
        
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
            return [comboCapitalCity, comboTimezone, comboKeyboard] 
        }, 0)
    }
    
    // Auto-fill function when capital city is selected
    function onCapitalCityChanged() {
        if (comboCapitalCity.currentIndex === -1) return
        
        var selectedCity = comboCapitalCity.currentText || comboCapitalCity.editText
        if (!selectedCity || selectedCity.length === 0) return
        
        var localeData = imageWriter.getLocaleDataForCapital(selectedCity)
        if (!localeData || Object.keys(localeData).length === 0) return
        
        // Auto-fill timezone if user hasn't manually changed it
        if (!userChangedTimezone && localeData.timezone) {
            var tzIndex = comboTimezone.find(localeData.timezone)
            if (tzIndex !== -1) comboTimezone.currentIndex = tzIndex
            else comboTimezone.editText = localeData.timezone
        }
        
        // Auto-fill keyboard if user hasn't manually changed it
        if (!userChangedKeyboard && localeData.keyboard) {
            var kbIndex = comboKeyboard.find(localeData.keyboard)
            if (kbIndex !== -1) comboKeyboard.currentIndex = kbIndex
            else comboKeyboard.editText = localeData.keyboard
        }
        
        // Save the recommended WiFi country for later
        if (localeData.countryCode) {
            var saved = imageWriter.getSavedCustomizationSettings()
            saved.recommendedWifiCountry = localeData.countryCode
            imageWriter.setSavedCustomizationSettings(saved)
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
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                WizardFormLabel { text: qsTr("Capital city:") }
                ImComboBox {
                    id: comboCapitalCity
                    Layout.fillWidth: true
                    editable: false
                    selectTextByMouse: true
                    font.pixelSize: Style.fontSizeInput
                    onActivated: {
                        // Use Qt.callLater to ensure text is fully updated
                        Qt.callLater(root.onCapitalCityChanged)
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                WizardFormLabel { 
                    text: qsTr("Time zone:") 
                }
                ImComboBox {
                    id: comboTimezone
                    Layout.fillWidth: true
                    editable: false
                    selectTextByMouse: true
                    font.pixelSize: Style.fontSizeInput
                    onActivated: {
                        root.userChangedTimezone = true
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                
                WizardFormLabel { 
                    text: qsTr("Keyboard layout:") 
                }
                ImComboBox {
                    id: comboKeyboard
                    Layout.fillWidth: true
                    editable: false
                    selectTextByMouse: true
                    font.pixelSize: Style.fontSizeInput
                    onActivated: {
                        root.userChangedKeyboard = true
                    }
                }
            }
            
        }
    }
    ]
    
    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        var city = comboCapitalCity.editText ? comboCapitalCity.editText.trim() : ""
        var tz = comboTimezone.editText ? comboTimezone.editText.trim() : ""
        var kb = comboKeyboard.editText ? comboKeyboard.editText.trim() : ""
        if (city.length > 0) saved.capitalCity = city; else delete saved.capitalCity
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
