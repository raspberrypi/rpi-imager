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
    subtitle: qsTr("Configure hostname settings")
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
                    id: chkHostname
                    text: qsTr("Set hostname")
                    checked: true
                }
                
                TextField {
                    id: fieldHostname
                    Layout.fillWidth: true
                    placeholderText: qsTr("raspberrypi")
                    enabled: chkHostname.checked
                    font.pixelSize: Style.fontSizeInput
                    
                    Component.onCompleted: {
                        text = "raspberrypi"
                    }
                    
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
    
    // Save settings when moving to next step
    onNextClicked: {
        var settings = {}
        
        if (chkHostname.checked && fieldHostname.text) {
            settings.hostname = fieldHostname.text
            wizardContainer.hostnameConfigured = true
        } else {
            wizardContainer.hostnameConfigured = false
        }
        
        // Store settings temporarily
        console.log("Hostname settings:", JSON.stringify(settings))
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