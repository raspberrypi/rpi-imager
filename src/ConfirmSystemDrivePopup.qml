// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd
import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.15
import "qmlcomponents"
import RpiImager

ImPopup {
    id: root

    property string driveName: ""
    property string device: ""
    property string sizeStr: ""
    property var mountpoints: []

    signal confirmed()
    signal cancelled()

    title: qsTr("DANGER: System drive selected")
    modal: true

    // Shared warning strings to align with other dialogs
    readonly property string riskText: CommonStrings.warningRiskText
    readonly property string proceedText: CommonStrings.warningProceedText

    // Provide implementation for the base popup's navigation functions
    function getNextFocusableElement(startElement) {
        var focusableItems = [nameInput, continueButton, cancelButton, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })

        if (focusableItems.length === 0) return startElement;

        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[0];

        var nextIndex = (currentIndex + 1) % focusableItems.length;
        return focusableItems[nextIndex];
    }

    function getPreviousFocusableElement(startElement) {
        var focusableItems = [nameInput, continueButton, cancelButton, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })

        if (focusableItems.length === 0) return startElement;

        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[0];

        var prevIndex = (currentIndex - 1 + focusableItems.length) % focusableItems.length;
        return focusableItems[prevIndex];
    }

    ColumnLayout {
        spacing: 14

        Text {
            Layout.margins: 10
            Layout.fillWidth: true
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap
            font.family: Style.fontFamily
            font.pointSize: 12
            text: qsTr("You are about to select a <b>SYSTEM DRIVE</b>: <b>%1</b>.").arg(root.driveName) + "<br><br>" + root.riskText + "<br><br>" + root.proceedText
        }

        Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.popupBorderColor }

        ColumnLayout {
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.fillWidth: true

            Text { text: qsTr("Device: %1").arg(root.device); font.family: Style.fontFamily; color: Style.textDescriptionColor }
            Text { text: qsTr("Size: %1").arg(root.sizeStr); font.family: Style.fontFamily; color: Style.textDescriptionColor }
            Text {
                text: qsTr("Mounted as: %1").arg(root.mountpoints && root.mountpoints.length > 0 ? root.mountpoints.join(", ") : qsTr("Not mounted"))
                font.family: Style.fontFamily
                color: Style.textDescriptionColor
            }
        }

        Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.popupBorderColor }

        Text {
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 4
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.family: Style.fontFamily
            font.pointSize: 12
            text: qsTr("To continue, type the exact drive name below:")
        }

        Text {
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 0
            font.family: Style.fontFamily
            font.bold: true
            color: Style.subtitleColor
            text: root.driveName
        }

        TextField {
            id: nameInput
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.fillWidth: true
            placeholderText: qsTr("Type drive name exactly as shown above")
            text: ""
            onAccepted: {
                if (continueButton.enabled) {
                    continueButton.clicked()
                }
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && event.modifiers & Qt.ShiftModifier)) {
                    root.getPreviousFocusableElement(nameInput).forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab) {
                    root.getNextFocusableElement(nameInput).forceActiveFocus()
                    event.accepted = true
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter | Qt.AlignBottom
            Layout.bottomMargin: 10
            spacing: 20

            ImButtonRed {
                id: cancelButton
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

    onClosed: {
        if (visible) return
    }
}


