/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.15
import "qmlcomponents"

import RpiImager

OptionsTabBase {
    id: root

    property alias fieldHostname: fieldHostname
    property alias fieldUserName: fieldUserName
    property alias fieldTimezone: fieldTimezone
    property alias fieldWifiCountry: fieldWifiCountry
    property alias fieldKeyboardLayout: fieldKeyboardLayout
    property alias fieldWifiPassword: fieldWifiPassword
    property alias fieldWifiSSID: fieldWifiSSID
    property alias chkWifi: chkWifi
    property alias chkWifiSSIDHidden: chkWifiSSIDHidden
    property alias chkHostname: chkHostname
    property alias chkLocale: chkLocale
    property alias chkSetUser: chkSetUser
    property alias fieldUserPassword: fieldUserPassword

    required property bool sshEnabled
    required property bool passwordAuthenticationEnabled

    ColumnLayout {
        // Ensure layout doesn't interfere with tab navigation
        activeFocusOnTab: false
        
        RowLayout {
            ImCheckBox {
                id: chkHostname
                text: qsTr("Set hostname:")
                
                // Handle explicit navigation in both directions
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        fieldHostname.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        // Navigate back to TabBar
                        if (root.tabBar) {
                            root.tabBar.forceActiveFocus()
                            event.accepted = true
                        }
                    }
                }
                
                onCheckedChanged: {
                    if (checked) {
                        fieldHostname.forceActiveFocus()
                        // Re-validate existing content when section is re-enabled
                        if (fieldHostname.text.length > 0 && !fieldHostname.acceptableInput) {
                            fieldHostname.indicateError = true
                        }
                    } else {
                        // Clear error state when hostname section is disabled
                        fieldHostname.indicateError = false
                    }
                }
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            TextField {
                id: fieldHostname
                enabled: chkHostname.checked
                text: "raspberrypi"
                selectByMouse: true
                maximumLength: 253
                validator: RegularExpressionValidator { regularExpression: /[0-9A-Za-z][0-9A-Za-z-]{0,62}/ }
                Layout.minimumWidth: 200
                
                // Handle explicit navigation
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        chkSetUser.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        chkHostname.forceActiveFocus()
                        event.accepted = true
                    }
                }
                
                property bool indicateError: false
                
                // Visual feedback for validation errors
                color: indicateError ? Style.formLabelErrorColor : (enabled ? "black" : "grey")
                
                onTextChanged: {
                    // Always validate on text change and update error state accordingly
                    if (text.length > 0 || acceptableInput) {
                        indicateError = false
                    } else {
                        indicateError = true
                    }
                }
                
                onEditingFinished: {
                    // Final validation check when editing is finished
                    if (text.length > 0 && !acceptableInput) {
                        indicateError = true
                    } else {
                        indicateError = false
                    }
                }
            }
            Text {
                text : ".local"
                color: chkHostname.checked ? Style.formLabelColor : Style.formLabelColor
            }
        }

        ImCheckBox {
            id: chkSetUser
            text: qsTr("Set username and password")
            
            // Handle explicit navigation
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                    fieldUserName.forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                    // Go to hostname field if enabled, otherwise hostname checkbox
                    if (chkHostname.checked) {
                        fieldHostname.forceActiveFocus()
                    } else {
                        chkHostname.forceActiveFocus()
                    }
                    event.accepted = true
                }
            }
            
            onCheckedChanged: {
                if (!checked && root.sshEnabled && root.passwordAuthenticationEnabled) {
                    checked = true;
                }
                if (checked && !fieldUserPassword.length) {
                    fieldUserPassword.forceActiveFocus()
                } else if (checked) {
                    // Re-validate existing content when section is re-enabled
                    if (fieldUserName.text.length > 0 && !fieldUserName.acceptableInput) {
                        fieldUserName.indicateError = true
                    }
                    if (fieldUserPassword.text.length === 0) {
                        fieldUserPassword.indicateError = true
                    }
                } else if (!checked) {
                    // Clear error states when user account section is disabled
                    fieldUserName.indicateError = false
                    fieldUserPassword.indicateError = false
                }
            }
        }

        RowLayout {
            Text {
                text: qsTr("Username:")
                color: parent.enabled ? (fieldUserName.indicateError ? Style.formLabelErrorColor : Style.formLabelColor) : Style.formLabelColor
                Layout.leftMargin: 40
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            TextField {
                id: fieldUserName
                enabled: chkSetUser.checked
                text: "pi"
                Layout.minimumWidth: 200
                selectByMouse: true
                property bool indicateError: false
                maximumLength: 31
                validator: RegularExpressionValidator { regularExpression: /^[a-z][a-z0-9-]{0,30}$/ }
                
                // Handle explicit navigation
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        fieldUserPassword.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        chkSetUser.forceActiveFocus()
                        event.accepted = true
                    }
                }
                
                // Visual feedback for validation errors
                color: indicateError ? Style.formLabelErrorColor : (enabled ? "black" : "grey")

                onTextChanged: {
                    // Always validate on text change and update error state accordingly
                    if (text.length > 0 || acceptableInput) {
                        indicateError = false
                    } else {
                        indicateError = true
                    }
                }
                
                onEditingFinished: {
                    // Final validation check when editing is finished
                    if (text.length > 0 && !acceptableInput) {
                        indicateError = true
                    } else {
                        indicateError = false
                    }
                }
            }
        }
        RowLayout {
            Text {
                text: qsTr("Password:")
                color: parent.enabled ? (fieldUserPassword.indicateError ? Style.formLabelErrorColor : Style.formLabelColor) : Style.formLabelColor
                Layout.leftMargin: 40
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            TextField {
                id: fieldUserPassword
                enabled: chkSetUser.checked
                echoMode: TextInput.Password
                passwordMaskDelay: 2000 //ms
                Layout.minimumWidth: 200
                selectByMouse: true
                property bool alreadyCrypted: false
                property bool indicateError: false
                
                // Handle navigation and password clearing
                Keys.onPressed: (event) => {
                    // Handle navigation keys first
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        // Always go to chkWifi first
                        chkWifi.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        // Go to username field if user enabled, otherwise user checkbox
                        if (chkSetUser.checked) {
                            fieldUserName.forceActiveFocus()
                        } else {
                            chkSetUser.forceActiveFocus()
                        }
                        event.accepted = true
                    } else if (alreadyCrypted && isContentKey(event.key)) {
                        /* User is trying to edit saved (crypted) password with actual content, clear field */
                        alreadyCrypted = false
                        clear()
                        // Don't mark as accepted so the key input can proceed normally
                        event.accepted = false
                    }
                    
                    // Don't mark as accepted for other keys so they can propagate
                    if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab) {
                        event.accepted = false
                    }
                }
                
                // Helper function to determine if a key represents content input
                function isContentKey(key) {
                    // Navigation and modifier keys that should NOT clear the password
                    var navigationKeys = [
                        Qt.Key_Tab, Qt.Key_Backtab, Qt.Key_Left, Qt.Key_Right, 
                        Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End,
                        Qt.Key_PageUp, Qt.Key_PageDown, Qt.Key_Escape,
                        Qt.Key_Shift, Qt.Key_Control, Qt.Key_Alt, Qt.Key_Meta
                    ]
                    
                    return navigationKeys.indexOf(key) === -1
                }
                
                // Visual feedback for validation errors (password field uses different validation logic)
                color: indicateError ? Style.formLabelErrorColor : (enabled ? "black" : "grey")



                onTextChanged: {
                    // Password validation: cannot be empty when user account is enabled
                    if (text.length === 0) {
                        indicateError = true
                    } else {
                        indicateError = false
                    }
                }
            }
        }

        ImCheckBox {
            id: chkWifi
            text: qsTr("Configure wireless LAN")
            
            // Handle explicit navigation in both directions
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                    if (checked) {
                        // WiFi is enabled, go to WiFi SSID field
                        fieldWifiSSID.forceActiveFocus()
                    } else {
                        // WiFi is disabled, skip to locale checkbox
                        chkLocale.forceActiveFocus()
                    }
                    event.accepted = true
                } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                    // Go to password field if user enabled, otherwise user checkbox
                    if (chkSetUser.checked) {
                        fieldUserPassword.forceActiveFocus()
                    } else {
                        chkSetUser.forceActiveFocus()
                    }
                    event.accepted = true
                }
            }
            
            onCheckedChanged: {
                if (checked) {
                    if (!fieldWifiSSID.length) {
                        fieldWifiSSID.forceActiveFocus()
                    } else if (!fieldWifiPassword.length) {
                        fieldWifiPassword.forceActiveFocus()
                    }
                    
                    // Re-validate existing content when section is re-enabled
                    if (fieldWifiSSID.text.length === 0) {
                        fieldWifiSSID.indicateError = true
                    }
                    if (fieldWifiPassword.text.length > 0 && 
                        !(fieldWifiPassword.text.length >= 8 && fieldWifiPassword.text.length <= 64)) {
                        fieldWifiPassword.indicateError = true
                    }
                } else {
                    // Clear error states when Wi-Fi section is disabled
                    fieldWifiSSID.indicateError = false
                    fieldWifiPassword.indicateError = false
                }
            }
        }

        RowLayout {
            Text {
                text: qsTr("SSID:")
                color: chkWifi.checked ? (fieldWifiSSID.indicateError ? Style.formLabelErrorColor : Style.formLabelColor) : Style.formLabelColor
                Layout.leftMargin: 40
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            TextField {
                id: fieldWifiSSID
                // placeholderText: qsTr("SSID")
                enabled: chkWifi.checked
                Layout.minimumWidth: 200
                selectByMouse: true
                property bool indicateError: false
                
                // Handle explicit navigation
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        fieldWifiPassword.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        chkWifi.forceActiveFocus()
                        event.accepted = true
                    }
                }
                
                // Visual feedback for validation errors
                color: indicateError ? Style.formLabelErrorColor : (enabled ? "black" : "grey")
                
                onTextChanged: {
                    // Validate SSID: cannot be empty when Wi-Fi is enabled
                    if (text.length === 0) {
                        indicateError = true
                    } else {
                        indicateError = false
                    }
                }
            }
        }
        RowLayout {
            Text {
                text: qsTr("Password:")
                color: chkWifi.checked ? (fieldWifiPassword.indicateError ? Style.formLabelErrorColor : Style.formLabelColor) : Style.formLabelColor
                Layout.leftMargin: 40
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            TextField {
                id: fieldWifiPassword
                enabled: chkWifi.checked
                Layout.minimumWidth: 200
                selectByMouse: true
                echoMode: TextInput.Password
                property bool indicateError: false

                // Handle explicit navigation
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        chkWifiSSIDHidden.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        fieldWifiSSID.forceActiveFocus()
                        event.accepted = true
                    }
                }

                // Visual feedback for validation errors
                color: indicateError ? Style.formLabelErrorColor : (enabled ? "black" : "grey")

                onTextChanged: {
                    // Validate Wi-Fi password: 0 chars (open), 8-63 chars (passphrase), or 64 chars (hash)
                    if (text.length === 0 || (text.length >= 8 && text.length <= 64)) {
                        indicateError = false
                    } else {
                        indicateError = true
                    }
                }
            }
        }

        RowLayout {
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            ImCheckBox {
                id: chkWifiSSIDHidden
                enabled: chkWifi.checked
                Layout.columnSpan: 2
                text: qsTr("Hidden SSID")
                checked: false
                
                // Handle explicit navigation
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        fieldWifiCountry.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        fieldWifiPassword.forceActiveFocus()
                        event.accepted = true
                    }
                }
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Text {
                text: qsTr("Wireless LAN country:")
                color: chkWifi.checked ? Style.formLabelColor : Style.formLabelColor
                Layout.leftMargin: 40
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            ComboBox {
                id: fieldWifiCountry
                selectTextByMouse: true
                enabled: chkWifi.checked
                editable: true
                
                // Handle explicit navigation in both directions
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        // Navigate to locale checkbox
                        chkLocale.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        // Go back through WiFi fields if enabled
                        if (chkWifi.checked) {
                            chkWifiSSIDHidden.forceActiveFocus()
                        } else {
                            chkWifi.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                }
            }
        }

        ImCheckBox {
            id: chkLocale
            text: qsTr("Set locale settings")
            
            // Handle explicit navigation in both directions
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                    // Forward: go to timezone dropdown
                    if (checked) {
                        fieldTimezone.forceActiveFocus()
                    } else {
                        // Locale disabled, navigate to buttons
                        if (root.navigateToButtons) {
                            root.navigateToButtons()
                        }
                    }
                    event.accepted = true
                } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                    // Backward: conditional navigation based on WiFi state
                    if (chkWifi.checked) {
                        // WiFi is enabled, go to country combo
                        fieldWifiCountry.forceActiveFocus()
                    } else {
                        // WiFi is disabled, skip back to WiFi checkbox
                        chkWifi.forceActiveFocus()
                    }
                    event.accepted = true
                }
            }
        }
        RowLayout {
            Text {
                text: qsTr("Time zone:")
                color: chkLocale.checked ? Style.formLabelColor : Style.formLabelColor
                Layout.leftMargin: 40
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            ComboBox {
                enabled: chkLocale.checked
                selectTextByMouse: true
                id: fieldTimezone
                editable: true
                Layout.minimumWidth: 200
                
                // Handle explicit navigation
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        fieldKeyboardLayout.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        chkLocale.forceActiveFocus()
                        event.accepted = true
                    }
                }
            }
        }

        RowLayout {
            Text {
                text: qsTr("Keyboard layout:")
                color: chkLocale.checked ? Style.formLabelColor : Style.formLabelColor
                Layout.leftMargin: 40
            }
            // Spacer item
            Item {
                Layout.fillWidth: true
            }
            ComboBox {
                enabled: chkLocale.checked
                selectTextByMouse: true
                id: fieldKeyboardLayout
                editable: true
                Layout.minimumWidth: 200
                
                // Handle explicit navigation in both directions
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        // Navigate to Cancel/Save buttons
                        if (root.navigateToButtons) {
                            root.navigateToButtons()
                            event.accepted = true
                        }
                    } else if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                        // Go to timezone field if locale enabled, otherwise locale checkbox
                        if (chkLocale.checked) {
                            fieldTimezone.forceActiveFocus()
                        } else {
                            chkLocale.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                }
            }
        }
    }
}
