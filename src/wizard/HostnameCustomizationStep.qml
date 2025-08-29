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
    
    title: qsTr("Customization: Choose hostname")
    showSkipButton: true
    
    Component.onCompleted: {
        root.registerFocusGroup("hostname_fields", function(){ return [fieldHostname] }, 0)
        
        // Set initial focus on the hostname field
        root.initialFocusItem = fieldHostname
        
        // Prefill from saved settings
        var saved = imageWriter.getSavedCustomizationSettings()
        if (saved.hostname) {
            fieldHostname.text = saved.hostname
            wizardContainer.hostnameConfigured = true
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
                
                ImTextField {
                    id: fieldHostname
                    Layout.fillWidth: true
                    placeholderText: qsTr("raspberrypi")
                    font.pixelSize: Style.fontSizeInput
                    
                    
                    validator: RegularExpressionValidator {
                        regularExpression: /^[a-zA-Z0-9][a-zA-Z0-9-]{0,62}$/
                    }
                }
            }
            
            WizardDescriptionText {
                text: qsTr("A hostname is a unique name that identifies your Raspberry Pi on the network. It should contain only letters, numbers, and hyphens.")
            }
        }
    }
    ]
    
    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy: any non-empty hostname is a customization
        var saved = imageWriter.getSavedCustomizationSettings()
        var hostnameText = fieldHostname.text ? fieldHostname.text.trim() : ""
        if (hostnameText.length > 0) {
            saved.hostname = hostnameText
            wizardContainer.hostnameConfigured = true
        } else {
            // Empty -> remove from persisted settings
            delete saved.hostname
            wizardContainer.hostnameConfigured = false
        }
        imageWriter.setSavedCustomizationSettings(saved)
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