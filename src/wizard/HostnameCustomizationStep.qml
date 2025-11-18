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
    
    title: qsTr("Customisation: Choose hostname")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save hostname and continue to next customisation step")
    backButtonAccessibleDescription: qsTr("Return to previous step")
    skipButtonAccessibleDescription: qsTr("Skip all customisation and proceed directly to writing the image")
    
    Component.onCompleted: {
        root.registerFocusGroup("hostname_fields", function(){ return [helpText, fieldHostname] }, 0)
        
        // Initial focus will automatically go to title, then help text, then field (handled by WizardStepBase)
        
        // Prefill from conserved customization settings
        if (wizardContainer.customizationSettings.hostname) {
            fieldHostname.text = wizardContainer.customizationSettings.hostname
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
                    placeholderText: qsTr("Enter your hostname")
                    font.pixelSize: Style.fontSizeInput
                    Accessible.description: qsTr("A hostname is a unique name that identifies your Raspberry Pi on the network. It should contain only letters, numbers, and hyphens.")
                    
                    validator: RegularExpressionValidator {
                        regularExpression: /^[a-zA-Z0-9][a-zA-Z0-9-]{0,62}$/
                    }
                }
            }
            
            WizardDescriptionText {
                id: helpText
                text: qsTr("A hostname is a unique name that identifies your Raspberry Pi on the network. It should contain only letters, numbers, and hyphens.")
            }
        }
    }
    ]
    
    // Save settings when moving to next step
    onNextClicked: {
        var hostnameText = fieldHostname.text ? fieldHostname.text.trim() : ""
        
        // Update conserved customization settings (runtime state)
        if (hostnameText.length > 0) {
            wizardContainer.customizationSettings.hostname = hostnameText
            wizardContainer.hostnameConfigured = true
            // Persist for future sessions
            imageWriter.setPersistedCustomisationSetting("hostname", hostnameText)
        } else {
            // Empty -> remove from both runtime and persistent settings
            delete wizardContainer.customizationSettings.hostname
            wizardContainer.hostnameConfigured = false
            imageWriter.removePersistedCustomisationSetting("hostname")
        }
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
