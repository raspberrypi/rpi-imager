/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    title: qsTr("Customisation: Choose hostname")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save hostname and continue to next customisation step")
    backButtonAccessibleDescription: qsTr("Return to previous step")
    skipButtonAccessibleDescription: qsTr("Skip all customisation and proceed directly to writing the image")
    
    // The one rule, used by the field's validator and by the check on the
    // restore path below. Written once so the two cannot drift: a hostname
    // that can no longer be typed must not be able to arrive by another
    // route either.
    readonly property var hostnameRule: /^[a-zA-Z0-9][a-zA-Z0-9-]{0,62}$/

    // Set when a saved hostname was refused, so the step can say so rather
    // than presenting an empty field with no explanation.
    property string rejectedHostname: ""

    // Quoted back to the user, so it is held to printable characters and a
    // sensible length. A value typed here cannot contain either problem;
    // one written by hand into the settings file can.
    readonly property string rejectedHostnameShown: {
        var out = ""
        for (var i = 0; i < rejectedHostname.length && out.length < 64; ++i) {
            var c = rejectedHostname.charCodeAt(i)
            out += (c < 0x20 || c === 0x7F) ? "?" : rejectedHostname.charAt(i)
        }
        return out + (rejectedHostname.length > out.length ? "\u2026" : "")
    }

    Component.onCompleted: {
        root.registerFocusGroup("hostname_fields", function(){ 
            // Only include help text when screen reader is active (otherwise it's not focusable)
            var items = []
            if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
                items.push(helpText)
            }
            items.push(fieldHostname)
            return items
        }, 0)
        
        // Initial focus will automatically go to title, then help text, then field (handled by WizardStepBase)
        
        // Prefill from conserved customization settings.
        //
        // Checked rather than trusted. Assigning text does not run the
        // validator -- Qt only clears acceptableInput, which nothing here
        // reads -- so a hostname saved by an older version, edited by hand
        // in the settings file, or left behind by a tightening of the rules
        // would otherwise be restored and written back out on Next.
        var saved = wizardContainer.customizationSettings.hostname
        if (saved) {
            if (root.hostnameRule.test(saved)) {
                fieldHostname.text = saved
                wizardContainer.hostnameConfigured = true
            } else {
                root.rejectedHostname = saved
                wizardContainer.hostnameConfigured = false
            }
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
                    objectName: "hostnameField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Enter your hostname")
                    font.pointSize: Style.fontSizeInput
                    Accessible.description: qsTr("A hostname is a unique name that identifies your Raspberry Pi on the network. It should contain only letters, numbers, and hyphens.")
                    trimWhitespace: true

                    validator: RegularExpressionValidator {
                        regularExpression: root.hostnameRule
                    }
                }
            }
            
            WizardDescriptionText {
                id: helpText
                text: qsTr("A hostname is a unique name that identifies your Raspberry Pi on the network. It should contain only letters, numbers, and hyphens.")
            }

            WizardDescriptionText {
                id: rejectedNotice
                objectName: "hostnameRejectedNotice"
                visible: root.rejectedHostname.length > 0
                color: Style.formLabelErrorColor
                text: qsTr("The saved hostname \u201C%1\u201D is no longer allowed, so the box has been left empty.")
                          .arg(root.rejectedHostnameShown)
                Accessible.role: Accessible.AlertMessage
                Accessible.name: text
                Accessible.ignored: !visible
            }
        }
    }
    ]
    
    // Save settings when moving to next step
    onNextClicked: {
        var hostnameText = fieldHostname.value
        
        // Update conserved customization settings (runtime state)
        if (hostnameText.length > 0) {
            wizardContainer.customizationSettings.hostname = hostnameText
            wizardContainer.hostnameConfigured = true
            // Persist for future sessions
            ImageWriterSingleton.setPersistedCustomisationSetting("hostname", hostnameText)
        } else {
            // Empty -> remove from both runtime and persistent settings
            delete wizardContainer.customizationSettings.hostname
            wizardContainer.hostnameConfigured = false
            ImageWriterSingleton.removePersistedCustomisationSetting("hostname")
        }
    }
    
    // Handle skip button
    // Skipping means skipping all of it, wherever the button is pressed.
    onSkipClicked: wizardContainer.skipAllCustomisation()
} 
