/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
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

            // Status line (only when enabled)
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingSmall
                visible: useTokenPill.checked
                WizardFormLabel { text: qsTr("Status:") }
                Text {
                    Layout.fillWidth: true
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.textDescriptionColor
                    text: root.connectTokenReceived ? qsTr("Token received from browser") : qsTr("Waiting for token")
                }
            }
        }
    }
    ]

    // Token state and parsing helpers
    property bool connectTokenReceived: false
    property string connectToken: ""

    Connections {
        target: root.imageWriter

        // Listen for callback with token
        function onConnectTokenReceived(token){
            if (token && token.length > 0) {
                connectTokenReceived = true
                connectToken = token
            }
        }
    }

    Component.onCompleted: {
        root.registerFocusGroup("pi_connect", function(){ return [btnOpenConnect, useTokenPill.focusItem] }, 0)

        var token = root.imageWriter.getRuntimeConnectToken()
        if (token && token.length > 0) {
            connectTokenReceived = true
            connectToken = token
        }

        // Never load token from persistent settings; token is session-only
        // auto enable if token has already been provided
        if (connectTokenReceived) {
            useTokenPill.checked = true
            wizardContainer.piConnectEnabled = true
        }
    }

    onNextClicked: {
        if (useTokenPill.checked) {
            wizardContainer.piConnectEnabled = true
        } else {
            wizardContainer.piConnectEnabled = false
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


