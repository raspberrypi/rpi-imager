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
    
    // For overlay parenting set by caller if needed
    property alias overlayParent: root.parent

    property url url
    property string version: ""

    // Custom escape handling
    function escapePressed() {
        root.reject()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("content", function(){ 
            // Only include text elements when screen reader is active (otherwise they're not focusable)
            if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
                return [titleText, descriptionText]
            }
            return []
        }, 0)
        registerFocusGroup("buttons", function(){ 
            return [yesButton, noButton] 
        }, 1)
    }

    // Dialog content
    FocusableHeading {
        id: titleText
        text: qsTr("Update available")
        font.pointSize: Style.fontSizeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
    }

    FocusableText {
        id: descriptionText
        text: root.version.length > 0
            ? qsTr("Imager version %1 is available. Would you like to visit the website to download it?").arg(root.version)
            : qsTr("There is a newer version of Imager available. Would you like to visit the website to download it?")
        wrapMode: Text.WordWrap
        font.pointSize: Style.fontSizeDescription
        font.family: Style.fontFamily
        color: Style.textDescriptionColor
        Layout.fillWidth: true
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Style.spacingSmall
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }

        ImButton {
            id: noButton
            text: CommonStrings.no
            accessibleDescription: qsTr("Continue using the current version of Raspberry Pi Imager")
            activeFocusOnTab: true
            onClicked: {
                root.reject()
            }
        }

        ImButtonRed {
            id: yesButton
            text: qsTr("Update")
            // Make the primary action button wider to encourage clicking
            Layout.minimumWidth: Style.buttonWidthMinimum * 1.5
            implicitWidth: Style.buttonWidthMinimum * 1.5
            accessibleDescription: qsTr("Open the Raspberry Pi website in your browser to download the latest version")
            activeFocusOnTab: true
            onClicked: {
                if (root.url && root.url.toString && root.url.toString().length > 0) {
                    if (ImageWriterSingleton) {
                        ImageWriterSingleton.openUrl(root.url)
                    } else {
                        Qt.openUrlExternally(root.url)
                    }
                }
                root.accept()
            }
        }
    }
}
