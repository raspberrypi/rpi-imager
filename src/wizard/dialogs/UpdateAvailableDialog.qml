/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../qmlcomponents"
import RpiImager

BaseDialog {
    id: root
    
    // For overlay parenting set by caller if needed
    property alias overlayParent: root.parent

    property url url

    // Custom escape handling
    function escapePressed() {
        root.reject()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("buttons", function(){ 
            return [yesButton, noButton] 
        }, 0)
    }

    // Dialog content
    Text {
        text: qsTr("Update available")
        font.pixelSize: Style.fontSizeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
    }

    Text {
        text: qsTr("There is a newer version of Imager available. Would you like to visit the website to download it?")
        wrapMode: Text.WordWrap
        font.pixelSize: Style.fontSizeDescription
        font.family: Style.fontFamily
        color: Style.textDescriptionColor
        Layout.fillWidth: true
    }

    RowLayout {
        Layout.fillWidth: true
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
            text: CommonStrings.yes
            accessibleDescription: qsTr("Open the Raspberry Pi website in your browser to download the latest version")
            activeFocusOnTab: true
            onClicked: {
                if (root.url && root.url.toString && root.url.toString().length > 0) {
                    Qt.openUrlExternally(root.url)
                }
                root.accept()
            }
        }
    }
}
