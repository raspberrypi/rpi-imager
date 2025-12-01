// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
        registerFocusGroup("warning", function(){ 
            // Only include warning text when screen reader is active (otherwise it's not focusable)
            return (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? [warningText] : []
        }, 0)
        registerFocusGroup("buttons", function(){ 
            return [keepFilterButton, showSystemButton] 
        }, 1)
    }

    // Dialog content
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
        Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
        focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
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
