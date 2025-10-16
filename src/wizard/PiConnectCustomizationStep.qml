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

    function parseTokenFromUrl(u) {
        // Handle QUrl or string, accept token/code/deploy_key/auth_key
        var s = ""
        try {
            if (u && typeof u.toString === 'function') s = u.toString(); else s = String(u)
        } catch(e) { s = String(u) }
        if (!s || s.length === 0) return ""
        var qIndex = s.indexOf('?')
        if (qIndex === -1) return ""
        var query = s.substring(qIndex+1)
        var parts = query.split('&')
        var token = ""
        for (var i = 0; i < parts.length; i++) {
            var kv = parts[i].split('=')
            if (kv.length >= 2) {
                var key = decodeURIComponent(kv[0])
                var val = decodeURIComponent(kv.slice(1).join('='))
                if (key === 'token' || key === 'code' || key === 'deploy_key' || key === 'auth_key') {
                    token = val
                    break
                }
            }
        }
        return token
    }

    Component.onCompleted: {
        root.registerFocusGroup("pi_connect", function(){ return [btnOpenConnect, useTokenPill.focusItem] }, 0)
        var saved = imageWriter.getSavedCustomizationSettings()
        // Never load token from persistent settings; token is session-only
        if (saved.piConnectEnabled === true || saved.piConnectEnabled === "true") {
            useTokenPill.checked = true
            wizardContainer.piConnectEnabled = true
        }
        // Listen for callback with token
        root.imageWriter.connectCallbackReceived.connect(function(url){
            var t = parseTokenFromUrl(url)
            if (t && t.length > 0) {
                connectTokenReceived = true
                connectToken = t
                imageWriter.setRuntimeConnectToken(t)
            }
        })
    }

    onNextClicked: {
        var saved = imageWriter.getSavedCustomizationSettings()
        if (useTokenPill.checked) {
            saved.piConnectEnabled = true
            wizardContainer.piConnectEnabled = true
        } else {
            delete saved.piConnectEnabled
            wizardContainer.piConnectEnabled = false
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
        wizardContainer.piConnectEnabled = false

        // Jump to writing step
        wizardContainer.jumpToStep(wizardContainer.stepWriting)
    }
}


