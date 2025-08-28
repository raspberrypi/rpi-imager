/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RpiImager

Dialog {
    id: root
    modal: true
    // For overlay parenting set by caller if needed
    property alias overlayParent: root.parent
    anchors.centerIn: parent
    width: 520
    standardButtons: Dialog.NoButton

    property url url

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
            text: qsTr("Update available")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        Text {
            text: qsTr("There is a newer version of Imager available. Would you like to visit the website to download it?")
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeDescription
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }

            ImButton {
                text: qsTr("No")
                onClicked: {
                    root.reject()
                }
            }

            ImButtonRed {
                text: qsTr("Yes")
                onClicked: {
                    if (root.url && root.url.toString && root.url.toString().length > 0) {
                        Qt.openUrlExternally(root.url)
                    }
                    root.accept()
                }
            }
        }
    }
}


