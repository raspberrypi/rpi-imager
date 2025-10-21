// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RpiImager
import "../../qmlcomponents"

BaseDialog {
    id: root
    
    // Override positioning for overlayParent support
    closePolicy: Popup.CloseOnEscape
    required property Item overlayParent
    parent: overlayParent
    anchors.centerIn: parent

    signal confirmed()
    signal cancelled()

    readonly property string riskText: CommonStrings.warningRiskText
    readonly property string proceedText: CommonStrings.warningProceedText
    readonly property string systemDriveText: CommonStrings.systemDriveText

    // Custom escape handling
    function escapePressed() {
        root.close()
        root.cancelled()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("buttons", function(){ 
            return [keepFilterButton, showSystemButton] 
        }, 0)
    }

    // Dialog content
    Text {
        text: qsTr("Remove system drive filter?")
        font.pixelSize: Style.fontSizeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
        Accessible.name: text
        Accessible.ignored: false
    }

    Text {
        id: warningText
        textFormat: Text.StyledText
        text: qsTr("By disabling system drive filtering, <b>system drives will be shown</b> in the list.")
              + "<br><br>"
              + root.systemDriveText
              + "<br><br>" + root.riskText + "<br><br>" + root.proceedText
        font.pixelSize: Style.fontSizeDescription
        font.family: Style.fontFamily
        color: Style.textDescriptionColor
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text.replace(/<[^>]+>/g, '')  // Strip HTML tags for accessibility
        Accessible.ignored: false
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }

        ImButtonRed {
            id: keepFilterButton
            text: qsTr("KEEP FILTER ON")
            accessibleDescription: qsTr("Keep system drives hidden to prevent accidental damage to your operating system")
            activeFocusOnTab: true
            onClicked: {
                root.close()
                root.cancelled()
            }
        }

        ImButton {
            id: showSystemButton
            text: qsTr("SHOW SYSTEM DRIVES")
            accessibleDescription: qsTr("Remove the safety filter and display system drives in the storage device list")
            activeFocusOnTab: true
            onClicked: {
                root.close()
                root.confirmed()
            }
        }
    }
}