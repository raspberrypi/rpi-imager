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

    // Whether this showing has been answered yet.
    //
    // onClosed below is the catch-all for a prompt that went away without
    // an answer -- clicked away, or closed from outside. Without this it
    // also fired behind the No button and the escape key, both of which
    // have already refused: the request was then answered twice, once by
    // the button and once on the way out. Yes did not do it, because
    // onClosed checks userAccepted, so refusing and agreeing behaved
    // differently for no reason a caller could see.
    property bool answeredThisTime: false

    function askForPermission() {
        root.userAccepted = false
        root.answeredThisTime = false
        open()
    }

    // Custom escape handling
    function escapePressed() {
        root.userAccepted = false
        root.answeredThisTime = true
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
            objectName: "keychainNoButton"
            text: CommonStrings.no
            accessibleDescription: qsTr("Skip keychain access and manually enter the Wi-Fi password")
            Layout.preferredWidth: 80
            activeFocusOnTab: true
            onClicked: {
                root.userAccepted = false
                root.answeredThisTime = true
                root.reject()
            }
        }

        ImButtonRed {
            id: yesButton
            objectName: "keychainYesButton"
            text: CommonStrings.yes
            accessibleDescription: qsTr("Retrieve the Wi-Fi password from the system keychain using administrator authentication")
            Layout.preferredWidth: 80
            activeFocusOnTab: true
            onClicked: {
                root.userAccepted = true
                root.answeredThisTime = true
                root.accept()
            }
        }
    }

    onClosed: {
        if (!root.answeredThisTime && !root.userAccepted) {
            root.rejected()
        }
    }
}
