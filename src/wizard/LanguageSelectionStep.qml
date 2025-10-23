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

        // Default to English (British) if present, else current language
        var idx = display.indexOf("English (British)")
        if (idx === -1) {
            var current = imageWriter.getCurrentLanguage()
            var wantedDisplay = (current === "English") ? "English (British)" : current
            idx = display.indexOf(wantedDisplay)
        }
        comboLanguage.currentIndex = idx !== -1 ? idx : 0

        // Focus first interactive control
        root.registerFocusGroup("language_controls", function(){ return [comboLanguage] }, 0)
        root.initialFocusItem = comboLanguage
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

                    WizardFormLabel { text: qsTr("Language:") }
                    ImComboBox {
                        id: comboLanguage
                        Layout.fillWidth: true
                        editable: false
                        selectTextByMouse: true
                        font.pixelSize: Style.fontSizeInput
                    }
                }
            }
        }
    ]

    onNextClicked: {
        var idx = comboLanguage.currentIndex
        if (idx >= 0 && idx < _internalLanguages.length) {
            var internalName = _internalLanguages[idx]
            if (internalName && internalName.length > 0)
                imageWriter.changeLanguage(internalName)
        }
    }
}


