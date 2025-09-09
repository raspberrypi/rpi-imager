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

    property string driveName: ""
    property string device: ""
    property string sizeStr: ""
    property var mountpoints: []

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
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap
            font.family: Style.fontFamily
            font.pixelSize: Style.fontSizeDescription
            color: Style.textDescriptionColor
            Layout.fillWidth: true
            text: root.riskText + "<br><br>" + root.systemDriveText + "<br><br>" + root.proceedText
        }

        Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.titleSeparatorColor }

        ColumnLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Size: %1").arg(root.sizeStr); font.family: Style.fontFamily; color: Style.textDescriptionColor }
            Text {
                text: qsTr("Mounted as: %1").arg(root.mountpoints && root.mountpoints.length > 0 ? root.mountpoints.join(", ") : qsTr("Not mounted"))
                font.family: Style.fontFamily
                color: Style.textDescriptionColor
            }
        }

        Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.titleSeparatorColor }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.family: Style.fontFamily
            font.pixelSize: Style.fontSizeDescription
            color: Style.textDescriptionColor
            text: qsTr("To continue, type the exact drive name below:")
        }

        Text {
            font.family: Style.fontFamily
            font.bold: true
            color: Style.textDescriptionColor
            text: root.driveName
        }

        TextField {
            id: nameInput
            Layout.fillWidth: true
            placeholderText: qsTr("Type drive name exactly as shown above")
            text: ""
            Keys.onPressed: (event) => {
                if ((event.key === Qt.Key_V && (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) ||
                    (event.key === Qt.Key_Insert && (event.modifiers & Qt.ShiftModifier))) {
                    event.accepted = true
                    return
                }
            }
            onAccepted: {
                if (continueButton.enabled) continueButton.clicked()
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MiddleButton
                onPressed: (mouse) => { mouse.accepted = true }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }

            ImButtonRed {
                text: qsTr("CANCEL")
                onClicked: {
                    root.close()
                    root.cancelled()
                }
            }

            ImButton {
                id: continueButton
                text: qsTr("CONTINUE")
                enabled: nameInput.text === root.driveName
                onClicked: {
                    if (!enabled) return
                    root.close()
                    root.confirmed()
                }
            }
        }
    }

    onOpened: {
        nameInput.text = ""
        nameInput.forceActiveFocus()
    }
}


