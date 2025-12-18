/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
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
    // Keep a mapping from display names to internal language names
    property var _internalLanguages: []

    title: qsTr("Welcome")
    subtitle: qsTr("Choose your language for Raspberry Pi Imager")
    showBackButton: false

    // Populate and preselect language on load
    Component.onCompleted: {
        var langs = imageWriter.getTranslations()
        _internalLanguages = langs
        // Build display names, substituting British English label
        var display = []
        for (var i = 0; i < langs.length; i++) {
            var name = langs[i]
            display.push(name === "English" ? "English (British)" : name)
        }
        comboLanguage.model = display

        // Pre-select based on current language (which may have been loaded from saved preference in main.cpp)
        // Fall back to English (British) if current language is not set
        var current = imageWriter.getCurrentLanguage()
        var idx = -1
        
        if (current && current.length > 0) {
            var currentDisplay = (current === "English") ? "English (British)" : current
            idx = display.indexOf(currentDisplay)
        }
        
        if (idx === -1) {
            idx = display.indexOf("English (British)")
        }
        comboLanguage.currentIndex = idx !== -1 ? idx : 0

        // Include label before combo box so users hear the explanation first (only focusable when screen reader is active)
        root.registerFocusGroup("language_controls", function(){ return [labelLanguage, comboLanguage] }, 0)
        // Initial focus will automatically go to title, then subtitle, then first control (handled by WizardStepBase)
    }

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
                        id: labelLanguage
                        text: qsTr("Language:") 
                    }
                    ImComboBox {
                        id: comboLanguage
                        Layout.fillWidth: true
                        editable: false
                        selectTextByMouse: true
                        font.pixelSize: Style.fontSizeInput
                        Accessible.description: qsTr("Select the language for the Raspberry Pi Imager interface")
                        onActivated: function(index) {
                            if (index >= 0 && index < _internalLanguages.length) {
                                var internalName = _internalLanguages[index]
                                if (internalName && internalName.length > 0)
                                    imageWriter.changeLanguage(internalName)
                            }
                        }
                    }
                }
            }
        }
    ]

    onNextClicked: {
        var idx = comboLanguage.currentIndex
        if (idx >= 0 && idx < _internalLanguages.length) {
            var internalName = _internalLanguages[idx]
            if (internalName && internalName.length > 0) {
                imageWriter.changeLanguage(internalName)
                // Persist the language selection so it's remembered for future sessions
                // This also ensures the language selector is always shown on next launch
                imageWriter.setSetting("savedLanguage", internalName)
            }
        }
    }
}


