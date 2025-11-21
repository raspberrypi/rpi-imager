/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Layouts
import RpiImager

// A labeled button styled for Imager; only the button clicks, not the whole row
Item {
    id: control
    property alias text: label.text
    // Optional help link next to the label
    property string helpLabel: ""
    property url helpUrl: ""
    property string btnText: ""
    // Allow custom accessibility description
    property string accessibleDescription: ""
    property alias enabled: optionButton.enabled
    signal clicked()

    // Expose the actual focusable control for tab navigation
    property alias focusItem: optionButton
    // Expose the help link for tab navigation (when visible)
    property alias helpLinkItem: helpText

    implicitHeight: Math.max(Style.buttonHeightStandard - 8, 28)
    implicitWidth: label.implicitWidth + optionButton.implicitWidth + Style.cardPadding

    RowLayout {
        anchors.fill: parent
        spacing: Style.spacingMedium

        // Text block (label + optional help) on the left
        ColumnLayout {
            id: textColumn
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Style.spacingXXSmall

            // Main label
            Text {
                id: label
                Layout.alignment: Qt.AlignVCenter
                font.family: Style.fontFamilyBold
                font.pixelSize: Style.fontSizeFormLabel
                font.bold: true
                color: optionButton.enabled ? Style.formLabelColor : Style.textDescriptionColor
                elide: Text.ElideRight
                
                // Ignore this for accessibility - the button will handle it
                Accessible.ignored: true
            }

            // Optional help link under the label
            Text {
                id: helpText
                Layout.alignment: Qt.AlignVCenter
                visible: control.helpLabel !== "" && control.helpUrl !== ""
                text: control.helpLabel
                font.family: Style.fontFamily
                font.pixelSize: Style.fontSizeDescription
                color: helpText.activeFocus ? Style.raspberryRed : Style.buttonForegroundColor
                font.underline: helpHover.hovered || helpText.activeFocus
                
                // Keyboard accessibility
                activeFocusOnTab: true
                focusPolicy: Qt.TabFocus
                
                // Accessibility properties
                Accessible.role: Accessible.Link
                Accessible.name: text
                Accessible.description: qsTr("Opens in browser")
                
                TapHandler {
                    cursorShape: Qt.PointingHandCursor
                    onTapped: Qt.openUrlExternally(control.helpUrl)
                }
                HoverHandler {
                    id: helpHover
                    acceptedDevices: PointerDevice.Mouse
                    cursorShape: Qt.PointingHandCursor
                }
                
                // Keyboard activation
                Keys.onEnterPressed: Qt.openUrlExternally(control.helpUrl)
                Keys.onReturnPressed: Qt.openUrlExternally(control.helpUrl)
                Keys.onSpacePressed: Qt.openUrlExternally(control.helpUrl)
                
                Accessible.onPressAction: Qt.openUrlExternally(control.helpUrl)
            }
        }

        // Flexible spacer to push the switch flush-right and align across rows
        Item { Layout.fillWidth: true }

        ImButton {
            id: optionButton
            Layout.alignment: Qt.AlignVCenter
            activeFocusOnTab: true
            text: control.btnText
            
            // Override accessibility to include the full context
            Accessible.name: {
                var desc = ""
                if (control.accessibleDescription !== "") {
                    desc = control.accessibleDescription
                } else if (control.helpLabel !== "") {
                    desc = control.helpLabel
                }
                // Format: "Label - ButtonText, Description"
                var fullName = label.text + " - " + control.btnText
                return desc !== "" ? (fullName + ", " + desc) : fullName
            }
            Accessible.description: ""

            onClicked: {
                control.clicked()
            }
        }
    }

    function forceActiveFocus() { optionButton.forceActiveFocus() }
}
