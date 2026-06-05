/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
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
        registerFocusGroup("content", function(){ 
            // Only include text elements when screen reader is active (otherwise they're not focusable)
            if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
                return [titleText, descriptionText, subText]
            }
            return []
        }, 0)
        registerFocusGroup("buttons", function(){ 
            return [yesButton, noButton] 
        }, 1)
    }

    // Dialog content goes directly into the BaseDialog's contentLayout
    FocusableHeading {
        id: titleText
        text: qsTr("Keychain Access")
        font.pointSize: Style.fontSizeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
    }

    FocusableText {
        id: descriptionText
        text: qsTr("Would you like to prefill the Wi‑Fi password from the system keychain?")
        wrapMode: Text.WordWrap
        color: Style.textDescriptionColor
        font.pointSize: Style.fontSizeDescription
        Layout.fillWidth: true
    }

    FocusableText {
        id: subText
        text: qsTr("This will require administrator authentication on macOS.")
        wrapMode: Text.WordWrap
        color: Style.textMetadataColor
        font.pointSize: Style.fontSizeSmall
        Layout.fillWidth: true
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
