// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RpiImager
import "../../qmlcomponents"

BaseDialog {
    id: root

    required property Item overlayParent
    parent: overlayParent
    anchors.centerIn: parent

    property string deviceSerial: ""
    property string deviceModel: ""
    property string keyFingerprint: ""
    property bool lockJtag: false

    signal confirmed()
    signal cancelled()

    function escapePressed() {
        root.close()
        root.cancelled()
    }

    Component.onCompleted: {
        registerFocusGroup("warning", function(){
            return (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? [warningText] : []
        }, 0)
        registerFocusGroup("input", function(){
            return [jtagLockCheck, confirmInput]
        }, 1)
        registerFocusGroup("buttons", function(){
            return [cancelButton, programButton]
        }, 2)
    }

    onOpened: {
        confirmInput.text = ""
    }

    // Large red warning
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: warningColumn.implicitHeight + Style.spacingMedium * 2
        color: "#FFEBEE"
        radius: Style.borderRadius

        ColumnLayout {
            id: warningColumn
            anchors.fill: parent
            anchors.margins: Style.spacingMedium

            Text {
                id: warningText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.family: Style.fontFamily
                font.pointSize: Style.fontSizeDescription
                font.bold: true
                color: "#C62828"
                text: qsTr("WARNING: OTP Programming is PERMANENT and IRREVERSIBLE")
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.family: Style.fontFamily
                font.pointSize: Style.fontSizeSmall
                color: "#C62828"
                text: qsTr("This operation will permanently program the secure boot public key hash into the device's one-time programmable (OTP) memory. Once programmed, this device will ONLY boot images signed with the corresponding private key. This action cannot be undone.")
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }

    Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.titleSeparatorColor; Accessible.ignored: true }

    // Device info
    ColumnLayout {
        Layout.fillWidth: true

        Text {
            text: qsTr("Device: %1").arg(root.deviceModel)
            font.family: Style.fontFamily
            font.pointSize: Style.fontSizeDescription
            color: Style.textDescriptionColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Text {
            text: qsTr("Serial: %1").arg(root.deviceSerial)
            font.family: Style.fontFamily
            font.pointSize: Style.fontSizeDescription
            color: Style.textDescriptionColor
            visible: root.deviceSerial !== ""
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Text {
            text: qsTr("Key fingerprint: %1").arg(root.keyFingerprint)
            font.family: Style.fontFamily
            font.pointSize: Style.fontSizeSm
            font.bold: true
            color: Style.textDescriptionColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.titleSeparatorColor; Accessible.ignored: true }

    // JTAG lock option
    ImCheckBox {
        id: jtagLockCheck
        text: qsTr("Also lock JTAG debug port (additional irreversible action)")
        checked: root.lockJtag
        onCheckedChanged: root.lockJtag = checked
        Accessible.name: text
    }

    Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.titleSeparatorColor; Accessible.ignored: true }

    // Typed confirmation
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.family: Style.fontFamily
        font.pointSize: Style.fontSizeDescription
        color: Style.textDescriptionColor
        text: qsTr("To confirm, type the device serial number below:")
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    Text {
        font.family: Style.fontFamily
        font.pointSize: Style.fontSizeSm
        font.bold: true
        color: Style.textDescriptionColor
        text: root.deviceSerial
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Serial to type: %1").arg(text)
    }

    TextField {
        id: confirmInput
        Layout.fillWidth: true
        font.family: Style.fontFamily
        font.pointSize: Style.fontSizeInput
        placeholderText: qsTr("Type device serial number exactly")
        text: ""
        activeFocusOnTab: true
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("Confirmation input. Type exactly: %1").arg(root.deviceSerial)
        Keys.onPressed: (event) => {
            if ((event.key === Qt.Key_V && (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) ||
                (event.key === Qt.Key_Insert && (event.modifiers & Qt.ShiftModifier))) {
                event.accepted = true
                return
            }
        }
        onAccepted: {
            if (programButton.enabled) programButton.clicked()
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

        ImButton {
            id: cancelButton
            text: qsTr("CANCEL")
            accessibleDescription: qsTr("Cancel OTP programming and return to previous screen")
            activeFocusOnTab: true
            onClicked: {
                root.close()
                root.cancelled()
            }
        }

        ImButtonRed {
            id: programButton
            text: qsTr("PROGRAM OTP")
            accessibleDescription: qsTr("Permanently program the secure boot key into device OTP memory")
            enabled: confirmInput.text === root.deviceSerial && root.deviceSerial !== ""
            activeFocusOnTab: true
            onClicked: {
                if (!enabled) return
                root.close()
                root.confirmed()
            }
        }
    }
}
