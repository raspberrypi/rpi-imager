/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../qmlcomponents"
import RpiImager

BaseDialog {
    id: root

    // Dynamic height calculation based on actual content
    height: {
        var totalHeight = Style.cardPadding * 2  // Top and bottom padding
        totalHeight += (titleText ? titleText.implicitHeight : 0) || 25
        totalHeight += (descriptionText ? descriptionText.implicitHeight : 0) || 20
        totalHeight += (subText ? subText.implicitHeight : 0) || 15
        totalHeight += (buttonRow ? buttonRow.implicitHeight : 0) || 40
        totalHeight += Style.spacingMedium * 3  // Spacing between elements
        return Math.max(160, totalHeight)
    }

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
    }

    Text {
        id: descriptionText
        text: qsTr("Would you like to prefill the Wi‑Fi password from the system keychain?")
        wrapMode: Text.WordWrap
        color: Style.textDescriptionColor
        font.pixelSize: Style.fontSizeDescription
        Layout.fillWidth: true
    }

    Text {
        id: subText
        text: qsTr("This will require administrator authentication on macOS.")
        wrapMode: Text.WordWrap
        color: Style.textMetadataColor
        font.pixelSize: Style.fontSizeSmall
        Layout.fillWidth: true
    }

    RowLayout {
        id: buttonRow
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }

        ImButton {
            id: noButton
            text: qsTr("No")
            Layout.preferredWidth: 80
            activeFocusOnTab: true
            onClicked: {
                root.userAccepted = false
                root.reject()
            }
        }

        ImButtonRed {
            id: yesButton
            text: qsTr("Yes")
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