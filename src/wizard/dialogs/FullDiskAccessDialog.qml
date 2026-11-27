/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "../../qmlcomponents"
import RpiImager

// Shown on leaving storage selection when macOS would refuse the helper the
// chosen device, so access is granted before customisation, not after it.
BaseDialog {
    id: root

    required property Item overlayParent
    parent: overlayParent
    anchors.centerIn: parent

    // Access is now allowed; the caller moves on.
    signal granted()

    property bool stillDenied: false

    function escapePressed() {
        root.close()
    }

    onOpened: stillDenied = false

    Component.onCompleted: {
        registerFocusGroup("content", function(){
            return root.stillDenied ? [titleText, descriptionText, deniedText]
                                    : [titleText, descriptionText]
        }, 0)
        registerFocusGroup("buttons", function(){
            return [cancelButton, settingsButton, continueButton]
        }, 1)
    }

    Text {
        id: titleText
        textFormat: Text.PlainText
        text: qsTr("Allow Full Disk Access")
        font.pointSize: Style.fontSizeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    Text {
        id: descriptionText
        textFormat: Text.PlainText
        text: qsTr("macOS will not let Raspberry Pi Imager write to the selected storage device. "
                   + "Allow 'Raspberry Pi Imager' under Full Disk Access in System Settings, "
                   + "in Privacy & Security.\n\n"
                   + "macOS may then ask you to reopen Raspberry Pi Imager. If it does, choose "
                   + "your device, operating system and storage again.")
        wrapMode: Text.WordWrap
        color: Style.textDescriptionColor
        font.pointSize: Style.fontSizeDescription
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    Text {
        id: deniedText
        textFormat: Text.PlainText
        text: qsTr("Full Disk Access is still off for Raspberry Pi Imager.")
        visible: root.stillDenied
        wrapMode: Text.WordWrap
        color: Style.formLabelErrorColor
        font.pointSize: Style.fontSizeSmall
        Layout.fillWidth: true
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }

        ImButton {
            id: cancelButton
            text: qsTr("Cancel")
            accessibleDescription: qsTr("Stay on storage selection")
            activeFocusOnTab: true
            onClicked: root.close()
        }

        ImButton {
            id: settingsButton
            text: qsTr("Open System Settings")
            accessibleDescription: qsTr("Open Full Disk Access in System Settings")
            activeFocusOnTab: true
            onClicked: ImageWriterSingleton.openFullDiskAccessSettings()
        }

        ImButtonRed {
            id: continueButton
            text: qsTr("Continue")
            accessibleDescription: qsTr("Check access again and continue")
            activeFocusOnTab: true
            onClicked: {
                if (ImageWriterSingleton.fullDiskAccessDenied()) {
                    root.stillDenied = true
                } else {
                    root.close()
                    root.granted()
                }
            }
        }
    }
}
