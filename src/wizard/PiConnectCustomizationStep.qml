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

    title: qsTr("Customisation: Raspberry Pi Connect")
    subtitle: qsTr("Sign in to receive a token and enable Raspberry Pi Connect.")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save Raspberry Pi Connect settings and continue to next customisation step")
    backButtonAccessibleDescription: qsTr("Return to previous step")
    skipButtonAccessibleDescription: qsTr("Skip all customisation and proceed directly to writing the image")

    // Only visible/active when the selected OS supports it
    visible: wizardContainer.piConnectAvailable
    enabled: wizardContainer.piConnectAvailable

    // Content
    content: [
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Style.sectionPadding
        spacing: Style.stepContentSpacing

        WizardSectionContainer {
            // Enable/disable Connect
            ImOptionPill {
                id: useTokenPill
                Layout.fillWidth: true
                text: qsTr("Enable Raspberry Pi Connect")
                accessibleDescription: qsTr("Enable secure remote access to your Raspberry Pi through the Raspberry Pi Connect cloud service")
                helpLabel: imageWriter.isEmbeddedMode() ? "" : qsTr("What is Raspberry Pi Connect?")
                helpUrl: imageWriter.isEmbeddedMode() ? "" : "https://www.raspberrypi.com/software/connect/"
                checked: false
                onToggled: function(isChecked) { 
                    wizardContainer.piConnectEnabled = isChecked
                    // Rebuild focus order when pill state changes
                    root.rebuildFocusOrder()
                }
            }

            // Request token button (only when enabled and no token present)
            ImButton {
                id: btnOpenConnect
                Layout.fillWidth: true
                text: qsTr("Open Raspberry Pi Connect")
                accessibleDescription: qsTr("Open the Raspberry Pi Connect website in your browser to sign in and receive an authentication token")
                enabled: useTokenPill.checked
                visible: useTokenPill.checked && !root.connectTokenReceived
                onClicked: {
                    if (root.imageWriter) {
                        var authUrl = Qt.resolvedUrl("https://connect.raspberrypi.com/imager/")
                        root.imageWriter.openUrl(authUrl)
                    }
                }
            }

            // Token input field label and field (only when enabled)
            WizardFormLabel {
                id: labelConnectToken
                text: qsTr("Authentication token:")
                visible: useTokenPill.checked
                accessibleDescription: qsTr("Enter or paste the authentication token from Raspberry Pi Connect. The token will be automatically filled if you use the 'Open Raspberry Pi Connect' button to sign in.")
            }
            
            ImTextField {
                id: fieldConnectToken
                Layout.fillWidth: true
                font.pixelSize: Style.fontSizeInput
                visible: useTokenPill.checked
                enabled: root.tokenFieldEnabled
                persistentSelection: true
                mouseSelectionMode: TextInput.SelectCharacters
                placeholderText: {
                    if (root.connectTokenReceived) {
                        return qsTr("Token received from browser")
                    } else if (root.countdownSeconds > 0) {
                        return qsTr("Waiting for token (%1s)").arg(root.countdownSeconds)
                    } else {
                        return qsTr("Paste token here")
                    }
                }
                text: root.connectToken
                onTextChanged: {
                    var token = text.trim()
                    if (token && token.length > 0) {
                        root.connectToken = token
                        countdownTimer.stop()
                        // Don't automatically validate or set as received - that happens on Next click
                    } else {
                        root.connectTokenReceived = false
                        root.connectToken = ""
                    }
                }
            }
        }
    }
    ]

    // Token state and parsing helpers
    property bool connectTokenReceived: false
    property string connectToken: ""
    property int countdownSeconds: 25
    property bool tokenFieldEnabled: false
    property bool tokenFromBrowser: false
    property bool isValid: false
    
    // Countdown timer
    Timer {
        id: countdownTimer
        interval: 1000
        repeat: true
        running: false
        onTriggered: {
            if (root.countdownSeconds > 0) {
                root.countdownSeconds--
            }
            if (root.countdownSeconds === 0) {
                stop()
                root.tokenFieldEnabled = true
                // Rebuild focus order since the text field is now enabled
                root.rebuildFocusOrder()
            }
        }
    }

    Connections {
        target: root.imageWriter

        // Listen for callback with token
        function onConnectTokenReceived(token){
            if (token && token.length > 0) {
                root.connectTokenReceived = true
                root.connectToken = token
                root.tokenFromBrowser = true
                // Stop the countdown but KEEP field disabled forever when from browser
                countdownTimer.stop()
                root.tokenFieldEnabled = false
                // Update the text field when token is received from browser
                if (fieldConnectToken) {
                    fieldConnectToken.text = token
                }
                // Rebuild focus order since the "Open Raspberry Pi Connect" button is now hidden
                root.rebuildFocusOrder()
            }
        }

        // Listen for token cleared signal (when write completes)
        function onConnectTokenCleared() {
            // Reset all Pi Connect customization state
            root.connectTokenReceived = false
            root.connectToken = ""
            root.tokenFromBrowser = false
            root.tokenFieldEnabled = false
            root.countdownSeconds = 25
            countdownTimer.stop()
            useTokenPill.checked = false
            root.wizardContainer.piConnectEnabled = false
            // Clear the text field
            if (fieldConnectToken) {
                fieldConnectToken.text = ""
            }
            // Clear from customization settings
            delete wizardContainer.customizationSettings.piConnectEnabled
            // Rebuild focus order
            root.rebuildFocusOrder()
        }
    }

    Component.onCompleted: {
        root.registerFocusGroup("pi_connect", function(){
            var items = [useTokenPill.focusItem]
            // Include help link if visible
            if (useTokenPill.helpLinkItem && useTokenPill.helpLinkItem.visible)
                items.push(useTokenPill.helpLinkItem)
            // Only include button if it's actually visible (checked and no token received yet)
            if (useTokenPill.checked && !root.connectTokenReceived)
                items.push(btnOpenConnect)
            // Include label before text field so users hear the explanation first
            if (useTokenPill.checked && fieldConnectToken.enabled) {
                items.push(labelConnectToken)
                items.push(fieldConnectToken)
            }
            return items
        }, 0)

        var token = root.imageWriter.getRuntimeConnectToken()
        if (token && token.length > 0) {
            root.connectTokenReceived = true
            root.connectToken = token
            // If token already exists, assume it came from browser (keep disabled)
            root.tokenFromBrowser = true
            root.tokenFieldEnabled = false
            // Update the text field with the existing token
            if (fieldConnectToken) {
                fieldConnectToken.text = token
            }
        }

        // Never load token from persistent settings; token is session-only
        // auto enable if token has already been provided
        if (root.connectTokenReceived) {
            useTokenPill.checked = true
            root.wizardContainer.piConnectEnabled = true
        }
    }
    
    // Start countdown when user clicks "Open Raspberry Pi Connect"
    Connections {
        target: btnOpenConnect
        function onClicked() {
            if (!root.connectTokenReceived && !countdownTimer.running) {
                root.countdownSeconds = 25
                countdownTimer.start()
            }
        }
    }

    onNextClicked: {
        // Reset validation state
        root.isValid = false
        
        if (useTokenPill.checked) {
            // Validate the token if user entered it manually (not from browser)
            if (!root.tokenFromBrowser) {
                var tokenToValidate = root.connectToken.trim()
                if (!tokenToValidate || tokenToValidate.length === 0) {
                    // No token provided
                    invalidTokenDialog.open()
                    return
                }
                
                // Validate token format
                var tokenIsValid = root.imageWriter.verifyAuthKey(tokenToValidate, true)
                //console.log("Token validation result:", tokenIsValid, "for token:", tokenToValidate)
                
                if (!tokenIsValid) {
                    // Token is invalid
                    invalidTokenDialog.open()
                    return
                }
                // Token is valid, set it in imageWriter
                root.imageWriter.overwriteConnectToken(tokenToValidate)
            }
            // Update conserved customization settings (runtime state only - NOT persisted)
            wizardContainer.customizationSettings.piConnectEnabled = true
            root.wizardContainer.piConnectEnabled = true
            root.isValid = true
        } else {
            // Not checked, remove from runtime settings
            delete wizardContainer.customizationSettings.piConnectEnabled
            root.wizardContainer.piConnectEnabled = false
            root.isValid = true
        }
        // Note: piConnectEnabled is intentionally NOT persisted to disk
        // It's a session-only setting tied to the ephemeral Connect token
    }
    
    // Invalid token dialog
    BaseDialog {
        id: invalidTokenDialog
        imageWriter: root.imageWriter
        parent: root.wizardContainer && root.wizardContainer.overlayRootRef ? root.wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        visible: false
        
        // Override height calculation to ensure proper sizing with wrapped text
        height: contentLayout ? Math.max(200, contentLayout.implicitHeight + Style.cardPadding * 2) : 200
        
        function escapePressed() {
            invalidTokenDialog.close()
        }
        
        Component.onCompleted: {
            registerFocusGroup("buttons", function(){ 
                return [okBtn] 
            }, 0)
        }
        
        // Dialog content
        Text {
            id: dialogTitle
            text: qsTr("Invalid Token")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            // Make accessible to screen readers
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        
        Text {
            id: dialogMessage
            text: qsTr("The token you entered is not valid. Please check the token and try again, or use the 'Open Raspberry Pi Connect' button to get a valid token.")
            font.pixelSize: Style.fontSizeFormLabel
            font.family: Style.fontFamily
            color: Style.formLabelColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            // Make accessible to screen readers
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }
            
            ImButton {
                id: okBtn
                text: qsTr("OK")
                accessibleDescription: qsTr("Close this dialog and return to the token field")
                activeFocusOnTab: true
                onClicked: {
                    invalidTokenDialog.close()
                    // Clear the invalid token
                    fieldConnectToken.text = ""
                    root.connectToken = ""
                    root.connectTokenReceived = false
                }
            }
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
        wizardContainer.piConnectEnabled = false

        // Jump to writing step
        wizardContainer.jumpToStep(wizardContainer.stepWriting)
    }
}


