/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.15
import "qmlcomponents"

import RpiImager

OptionsTabBase {
    id: root

    property alias publicKeyModel: publicKeyModel

    property alias chkSSH: chkSSH
    property alias radioPasswordAuthentication: radioPasswordAuthentication
    property alias radioPubKeyAuthentication: radioPubKeyAuthentication

    required property ImageWriter imageWriter
    required property ImCheckBox chkSetUser
    required property TextField fieldUserName
    required property TextField fieldUserPassword

    ColumnLayout {
        Layout.fillWidth: true

        ImCheckBox {
            id: chkSSH
            text: qsTr("Enable SSH")
            onCheckedChanged: {
                if (checked) {
                    if (!radioPasswordAuthentication.checked && !radioPubKeyAuthentication.checked) {
                        radioPasswordAuthentication.checked = true
                    }
                    if (radioPasswordAuthentication.checked) {
                        root.chkSetUser.checked = true
                    }
                }
            }
        }

        ImRadioButton {
            id: radioPasswordAuthentication
            enabled: chkSSH.checked
            text: qsTr("Use password authentication")
            onCheckedChanged: {
                if (checked) {
                    root.chkSetUser.checked = true
                    //root.fieldUserPassword.forceActiveFocus()
                }
            }
        }
        ImRadioButton {
            id: radioPubKeyAuthentication
            enabled: chkSSH.checked
            text: qsTr("Allow public-key authentication only")
            onCheckedChanged: {
                if (checked) {
                    if (root.chkSetUser.checked && root.fieldUserName.text == "pi" && root.fieldUserPassword.text.length == 0) {
                        root.chkSetUser.checked = false
                    }
                }
            }
        }

        Text {
            text: qsTr("Set authorized_keys for '%1':").arg(root.fieldUserName.text)
            color: radioPubKeyAuthentication.checked ? Style.formLabelColor : Style.formLabelDisabledColor
            // textFormat: Text.PlainText
            Layout.leftMargin: 40
        }
        Item {
            id: publicKeyListViewContainer
            Layout.leftMargin: 40
            Layout.rightMargin: 5
            Layout.minimumHeight: 50
            Layout.fillHeight: true
            Layout.preferredWidth: root.width - (20 + Layout.leftMargin + Layout.rightMargin)

            ListView {
                id: publicKeyList
                model: ListModel {
                    id: publicKeyModel

                }
                boundsBehavior: Flickable.StopAtBounds
                anchors.fill: parent
                spacing: 12
                clip: true

                delegate: RowLayout {
                    id: publicKeyItem
                    required property int index
                    readonly property int publicKeyModelIndex: index
                    required property string publicKeyField

                    height: 50

                    TextField {
                        id: contentField
                        enabled: radioPubKeyAuthentication.checked
                        validator: RegularExpressionValidator { regularExpression: /^ssh-(ed25519|rsa|dss|ecdsa) AAAA(?:[A-Za-z0-9+\/]{4})*(?:[A-Za-z0-9+\/]{2}==|[A-Za-z0-9+\/]{3}=|[A-Za-z0-9+\/]{4})( [A-Za-z0-9+\-\._\\]+@[A-Za-z0-9+\-\.]+)?/ }
                        text: publicKeyItem.publicKeyField
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        implicitWidth: publicKeyList.width - (removePublicKeyItem.width + 20)

                        onEditingFinished: {
                                publicKeyModel.set(publicKeyItem.publicKeyModelIndex, {publicKeyField: contentField.text})
                            }
                    }
                    ImButton {
                        id: removePublicKeyItem
                        text: qsTr("Delete Key")
                        enabled: root.radioPubKeyAuthentication.checked
                        Layout.minimumWidth: 100
                        Layout.preferredWidth: 100
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                        onClicked: {
                            if (publicKeyItem.publicKeyModelIndex != -1) {
                                publicKeyModel.remove(publicKeyItem.publicKeyModelIndex)
                                publicKeyListViewContainer.implicitHeight -= 50 + publicKeyList.spacing
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            ImButton {
                text: qsTr("RUN SSH-KEYGEN")
                Layout.leftMargin: 40
                enabled: root.imageWriter.hasSshKeyGen() && !root.imageWriter.hasPubKey()
                onClicked: {
                    enabled = false
                    root.imageWriter.generatePubKey()
                    publicKeyModel.append({publicKeyField: root.imageWriter.getDefaultPubKey()})
                }
            }
            ImButton {
                text: qsTr("Add SSH Key")
                Layout.leftMargin: 40
                enabled: radioPubKeyAuthentication.checked
                onClicked: {
                    publicKeyModel.append({publicKeyField: ""})
                    if (publicKeyListViewContainer.implicitHeight) {
                        publicKeyListViewContainer.implicitHeight += 50 + publicKeyList.spacing
                    } else {
                        publicKeyListViewContainer.implicitHeight += 100 + (publicKeyList.spacing)
                    }
                }
            }
        }
    }
}
