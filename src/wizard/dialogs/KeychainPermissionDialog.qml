/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RpiImager

Dialog {
    id: root
    modal: true
    anchors.centerIn: parent
    width: 520
    standardButtons: Dialog.NoButton

    property bool userAccepted: false

    function askForPermission() {
        root.userAccepted = false
        open()
    }

    background: Rectangle {
        color: Style.mainBackgroundColor
        radius: Style.sectionBorderRadius
        border.color: Style.popupBorderColor
        border.width: Style.sectionBorderWidth
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.cardPadding
        spacing: Style.spacingMedium

        Text {
            text: qsTr("Keychain Access")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        Text {
            text: qsTr("Would you like to prefill the wifi password from the system keychain?")
            wrapMode: Text.WordWrap
            color: Style.textDescriptionColor
            font.pixelSize: Style.fontSizeDescription
            Layout.fillWidth: true
        }

        Text {
            text: qsTr("This will require administrator authentication on macOS.")
            wrapMode: Text.WordWrap
            color: Style.textMetadataColor
            font.pixelSize: Style.fontSizeSmall
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }

            ImButton {
                text: qsTr("No")
                Layout.preferredWidth: 80
                onClicked: {
                    root.userAccepted = false
                    root.reject()
                }
            }

            ImButtonRed {
                text: qsTr("Yes")
                Layout.preferredWidth: 80
                onClicked: {
                    root.userAccepted = true
                    root.accept()
                }
            }
        }
    }

    onClosed: {
        if (!root.userAccepted) {
            root.rejected()
        }
    }
}


