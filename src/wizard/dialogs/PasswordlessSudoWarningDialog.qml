/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../qmlcomponents"
import RpiImager

BaseDialog {
    id: root

    property bool userAccepted: false

    signal confirmed()
    signal cancelled()

    function askForConfirmation() {
        root.userAccepted = false
        open()
    }

    // Custom escape handling
    function escapePressed() {
        root.userAccepted = false
        root.close()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("content", function(){
            if (root.imageWriter && root.imageWriter.isScreenReaderActive()) {
                return [titleText, warningText, detailText]
            }
            return []
        }, 0)
        registerFocusGroup("buttons", function(){
            return [cancelButton, enableButton]
        }, 1)
    }

    // Dialog content
    Text {
        id: titleText
        text: qsTr("Passwordless Sudo")
        font.pointSize: Style.fontSizeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelErrorColor
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
        Accessible.name: text
        Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
        focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
    }

    Text {
        id: warningText
        text: qsTr("Enabling passwordless sudo allows any process running as this user to gain full root privileges without authentication. This significantly weakens the security of your system.")
        wrapMode: Text.WordWrap
        color: Style.textDescriptionColor
        font.pointSize: Style.fontSizeDescription
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
        focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
    }

    Text {
        id: detailText
        text: qsTr("Only enable this if you understand the risks and have a specific need, such as automated scripts or headless operation.")
        wrapMode: Text.WordWrap
        color: Style.textMetadataColor
        font.pointSize: Style.fontSizeSmall
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
        focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
    }

    RowLayout {
        id: buttonRow
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }

        ImButton {
            id: cancelButton
            text: qsTr("CANCEL")
            accessibleDescription: qsTr("Cancel and keep sudo requiring a password")
            activeFocusOnTab: true
            onClicked: {
                root.userAccepted = false
                root.close()
            }
        }

        ImButtonRed {
            id: enableButton
            text: qsTr("ENABLE")
            accessibleDescription: qsTr("Enable passwordless sudo for this user account")
            activeFocusOnTab: true
            onClicked: {
                root.userAccepted = true
                root.close()
            }
        }
    }

    onClosed: {
        if (root.userAccepted) {
            root.confirmed()
        } else {
            root.cancelled()
        }
    }
}
