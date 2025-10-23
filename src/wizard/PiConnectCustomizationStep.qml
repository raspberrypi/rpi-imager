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
                onToggled: function(isChecked) { wizardContainer.piConnectEnabled = isChecked }
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

            // Token input field (only when enabled)
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
                
                ContextMenu.menu: Menu {
                    MenuItem {
                        text: qsTr("Paste")
                        enabled: fieldConnectToken.enabled
                        onTriggered: {
                            var clipboardText = root.imageWriter.getClipboardText()
                            if (clipboardText && clipboardText.length > 0) {
                                fieldConnectToken.text = clipboardText.trim()
                                fieldConnectToken.forceActiveFocus()
                            }
                        }
                    }
                    
                    MenuItem {
                        text: qsTr("Select All")
                        enabled: fieldConnectToken.enabled && fieldConnectToken.text.length > 0
                        onTriggered: fieldConnectToken.selectAll()
                    }
                }
            }
        }
    }
    ]

    // Token state and parsing helpers
    property bool connectTokenReceived: false
    property string connectToken: ""
    property int countdownSeconds: 90
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
            }
        }
    }

    Component.onCompleted: {
        root.registerFocusGroup("pi_connect", function(){
            var items = [useTokenPill.focusItem]
            if (useTokenPill.checked)
                items.push(btnOpenConnect)
            if (fieldConnectToken.enabled)
                items.push(fieldConnectToken)
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
                root.countdownSeconds = 90
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
            root.wizardContainer.piConnectEnabled = true
            root.isValid = true
        } else {
            // Not checked, just allow to proceed
            root.wizardContainer.piConnectEnabled = false
            root.isValid = true
        }
    }
    
    // Invalid token dialog
    BaseDialog {
        id: invalidTokenDialog
        parent: root.wizardContainer && root.wizardContainer.overlayRootRef ? root.wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        visible: false
        
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
            text: qsTr("Invalid Token")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        
        Text {
            text: qsTr("The token you entered is not valid. Please check the token and try again, or use the 'Open Raspberry Pi Connect' button to get a valid token.")
            font.pixelSize: Style.fontSizeFormLabel
            font.family: Style.fontFamily
            color: Style.formLabelColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }
            
            ImButton {
                id: okBtn
                text: qsTr("OK")
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


