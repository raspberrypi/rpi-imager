/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
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
    
    title: qsTr("Customisation: Localisation")
    subtitle: qsTr("Select your location for suggested time zone and keyboard layout")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save localisation settings and continue to next customisation step")
    backButtonAccessibleDescription: qsTr("Return to previous step")
    skipButtonAccessibleDescription: qsTr("Skip all customisation and proceed directly to writing the image")

    // Initial focus will automatically go to title, then subtitle, then first control (handled by WizardStepBase)
    
    // Track if user manually changed fields (to avoid overwriting their choices)
    property bool userChangedTimezone: false
    property bool userChangedKeyboard: false

    // Initialize the component
    Component.onCompleted: {
        // Load capital cities, timezones and keyboard layout data
        comboCapitalCity.model = imageWriter.getCapitalCitiesList()
        comboTimezone.model = imageWriter.getTimezoneList()
        comboKeyboard.model = imageWriter.getKeymapLayoutList()
        
        // Prefill from conserved customization settings; fallback to platform defaults
        var settings = wizardContainer.customizationSettings
        
        // Restore saved capital city if available
        if (settings.capitalCity) {
            var cityIndex = comboCapitalCity.find(settings.capitalCity)
            if (cityIndex !== -1) {
                comboCapitalCity.currentIndex = cityIndex
                // Ensure WiFi country recommendation is always up to date
                // Call immediately (not deferred) so subsequent steps can see the value
                root.onCapitalCityChanged()
            }
        }
        
        var tzToSet = settings.timezone || imageWriter.getTimezone()
        var tzIndex = comboTimezone.find(tzToSet)
        if (tzIndex !== -1) comboTimezone.currentIndex = tzIndex
        else comboTimezone.editText = tzToSet

        var defaultKeyboard = (tzToSet === "Europe/London") ? "gb" : "us"
        var kbToSet = settings.keyboard || defaultKeyboard
        var kbIndex = comboKeyboard.find(kbToSet)
        if (kbIndex !== -1) comboKeyboard.currentIndex = kbIndex
        else comboKeyboard.editText = kbToSet

        // Register focus group for locale controls in proper tab order
        // Include labels before their corresponding controls so users hear the explanation first
        root.registerFocusGroup("locale_controls", function(){ 
            return [labelCapitalCity, comboCapitalCity, labelTimezone, comboTimezone, labelKeyboard, comboKeyboard] 
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
            wizardContainer.customizationSettings.recommendedWifiCountry = localeData.countryCode
            imageWriter.setPersistedCustomisationSetting("recommendedWifiCountry", localeData.countryCode)
            console.log("LocaleCustomizationStep: Saved recommendedWifiCountry:", localeData.countryCode)
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
                
                WizardFormLabel { 
                    id: labelCapitalCity
                    text: qsTr("Capital city:") 
                    Accessible.ignored: false
                    Accessible.focusable: true
                    Accessible.description: qsTr("Choose your nearest capital city. This will automatically recommend the correct time zone and keyboard layout for your region, and set the wireless regulatory domain for your country's Wi-Fi regulations.")
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }
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
                    id: labelTimezone
                    text: qsTr("Time zone:") 
                    Accessible.ignored: false
                    Accessible.focusable: true
                    Accessible.description: qsTr("Choose your time zone so your Raspberry Pi displays the correct local time. This is automatically recommended based on your capital city selection, but you can change it if the suggestion is incorrect.")
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
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
                    id: labelKeyboard
                    text: qsTr("Keyboard layout:") 
                    Accessible.ignored: false
                    Accessible.focusable: true
                    Accessible.description: qsTr("Choose your keyboard layout so keys produce the correct characters when typing. This is automatically recommended based on your capital city selection, but you can change it if you use a different keyboard layout.")
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
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
        var city = comboCapitalCity.editText ? comboCapitalCity.editText.trim() : ""
        var tz = comboTimezone.editText ? comboTimezone.editText.trim() : ""
        var kb = comboKeyboard.editText ? comboKeyboard.editText.trim() : ""
        
        // Update conserved customization settings (runtime state)
        if (city.length > 0) {
            wizardContainer.customizationSettings.capitalCity = city
            imageWriter.setPersistedCustomisationSetting("capitalCity", city)
        } else {
            delete wizardContainer.customizationSettings.capitalCity
            imageWriter.removePersistedCustomisationSetting("capitalCity")
        }
        
        if (tz.length > 0) {
            wizardContainer.customizationSettings.timezone = tz
            imageWriter.setPersistedCustomisationSetting("timezone", tz)
        } else {
            delete wizardContainer.customizationSettings.timezone
            imageWriter.removePersistedCustomisationSetting("timezone")
        }
        
        if (kb.length > 0) {
            wizardContainer.customizationSettings.keyboard = kb
            imageWriter.setPersistedCustomisationSetting("keyboard", kb)
        } else {
            delete wizardContainer.customizationSettings.keyboard
            imageWriter.removePersistedCustomisationSetting("keyboard")
        }
        
        wizardContainer.localeConfigured = (tz.length > 0 || kb.length > 0)
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
