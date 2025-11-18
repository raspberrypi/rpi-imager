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

    function askForPermission() {
        root.userAccepted = false
        open()
    }

    // Custom escape handling
    function escapePressed() {
        root.userAccepted = false
        root.reject()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("buttons", function(){ 
            return [yesButton, noButton] 
        }, 0)
    }

    // Dialog content goes directly into the BaseDialog's contentLayout
    Text {
        id: titleText
        text: qsTr("Keychain Access")
        font.pixelSize: Style.fontSizeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
        Accessible.name: text
        Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
        focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
    }

    Text {
        id: descriptionText
        text: qsTr("Would you like to prefill the Wi‑Fi password from the system keychain?")
        wrapMode: Text.WordWrap
        color: Style.textDescriptionColor
        font.pixelSize: Style.fontSizeDescription
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
        focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
    }

    Text {
        id: subText
        text: qsTr("This will require administrator authentication on macOS.")
        wrapMode: Text.WordWrap
        color: Style.textMetadataColor
        font.pixelSize: Style.fontSizeSmall
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
            id: noButton
            text: CommonStrings.no
            accessibleDescription: qsTr("Skip keychain access and manually enter the Wi-Fi password")
            Layout.preferredWidth: 80
            activeFocusOnTab: true
            onClicked: {
                root.userAccepted = false
                root.reject()
            }
        }

        ImButtonRed {
            id: yesButton
            text: CommonStrings.yes
            accessibleDescription: qsTr("Retrieve the Wi-Fi password from the system keychain using administrator authentication")
            Layout.preferredWidth: 80
            activeFocusOnTab: true
            onClicked: {
                root.userAccepted = true
                root.accept()
            }
        }
    }

    onClosed: {
        if (!root.userAccepted) {
            root.rejected()
        }
    }
}
