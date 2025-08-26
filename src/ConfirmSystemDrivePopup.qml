// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: root
    modal: true
    anchors.centerIn: Overlay.overlay
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.cardPadding
        spacing: Style.spacingMedium

        Text {
            text: qsTr("DANGER: System drive selected")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        Text {
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap
            font.family: Style.fontFamily
            font.pixelSize: Style.fontSizeDescription
            color: Style.textDescriptionColor
            Layout.fillWidth: true
            text: qsTr("You are about to select a <b>SYSTEM DRIVE</b>: <b>%1</b>.").arg(root.driveName) + "<br><br>" + root.riskText + "<br><br>" + root.proceedText
        }

        Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.titleSeparatorColor }

        ColumnLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Device: %1").arg(root.device); font.family: Style.fontFamily; color: Style.textDescriptionColor }
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
            color: Style.subtitleColor
            text: root.driveName
        }

        TextField {
            id: nameInput
            Layout.fillWidth: true
            placeholderText: qsTr("Type drive name exactly as shown above")
            text: ""
            // Block paste via common shortcuts (Ctrl/Meta+V, Shift+Insert)
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
            // Swallow middle/right-click paste and context menu
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


