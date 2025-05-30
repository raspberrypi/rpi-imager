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
        RowLayout {
            ImCheckBox {
                id: chkHostname
                text: qsTr("Set hostname:")
                onCheckedChanged: {
                    if (checked) {
                        fieldHostname.forceActiveFocus()
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
            }
            Text {
                text : ".local"
                color: chkHostname.checked ? Style.formLabelColor : Style.formLabelColor
            }
        }

        ImCheckBox {
            id: chkSetUser
            text: qsTr("Set username and password")
            onCheckedChanged: {
                if (!checked && root.sshEnabled && root.passwordAuthenticationEnabled) {
                    checked = true;
                }
                if (checked && !fieldUserPassword.length) {
                    fieldUserPassword.forceActiveFocus()
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

                onTextEdited: {
                    indicateError = false
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

                Keys.onPressed: (event)=> {
                    if (alreadyCrypted) {
                        /* User is trying to edit saved
                            (crypted) password, clear field */
                        alreadyCrypted = false
                        clear()
                    }

                    // Do not mark the event as accepted, so that it may
                    // propagate down to the underlying TextField.
                    event.accepted = false
                }

                onTextEdited: {
                    if (indicateError) {
                        indicateError = false
                    }
                }
            }
        }

        ImCheckBox {
            id: chkWifi
            text: qsTr("Configure wireless LAN")
            onCheckedChanged: {
                if (checked) {
                    if (!fieldWifiSSID.length) {
                        fieldWifiSSID.forceActiveFocus()
                    } else if (!fieldWifiPassword.length) {
                        fieldWifiPassword.forceActiveFocus()
                    }
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
                onTextEdited: {
                    indicateError = false
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
                onTextEdited: {
                    indicateError = false
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
            }
        }

        ImCheckBox {
            id: chkLocale
            text: qsTr("Set locale settings")
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
            }
        }
    }
}
