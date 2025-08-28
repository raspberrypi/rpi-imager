// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RpiImager
import "../../qmlcomponents"

Dialog {
    id: root
    modal: true
    // Explicit parent for overlay centering (required)
    required property Item overlayParent
    parent: overlayParent
    anchors.centerIn: parent
    width: 520
    standardButtons: Dialog.NoButton

    signal confirmed()
    signal cancelled()

    readonly property string riskText: CommonStrings.warningRiskText
    readonly property string proceedText: CommonStrings.warningProceedText
    readonly property string systemDriveText: CommonStrings.systemDriveText

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
            text: qsTr("Remove system drive filter?")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        Text {
            textFormat: Text.StyledText
            text: qsTr("By disabling system drive filtering, <b>system drives will be shown</b> in the list.")
                  + "<br><br>"
                  + root.systemDriveText
                  + "<br><br>" + root.riskText + "<br><br>" + root.proceedText
            font.pixelSize: Style.fontSizeDescription
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }

            ImButtonRed {
                text: qsTr("KEEP FILTER ON")
                onClicked: {
                    root.close()
                    root.cancelled()
                }
            }

            ImButton {
                text: qsTr("SHOW SYSTEM DRIVES")
                onClicked: {
                    root.close()
                    root.confirmed()
                }
            }
        }
    }
}


