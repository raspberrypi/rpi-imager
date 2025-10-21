/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RpiImager
import QtQuick.Controls.Material 2.15

// A labeled switch styled for Imager; only the switch toggles, not the whole row
Item {
    id: pill
    property alias text: label.text
    property bool checked: false
    // Optional help link next to the label
    property string helpLabel: ""
    property url helpUrl: ""
    // Allow custom accessibility description
    property string accessibleDescription: ""
    signal toggled(bool checked)

    // Expose the actual focusable control for tab navigation
    property alias focusItem: sw

    implicitHeight: Math.max(Style.buttonHeightStandard - 8, 28)
    implicitWidth: label.implicitWidth + sw.implicitWidth + Style.cardPadding
    
    // Make the label text ignore accessibility so only the switch is read
    // This prevents VoiceOver from reading the label separately

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
                color: Style.formLabelColor
                elide: Text.ElideRight
                TapHandler { onTapped: sw.toggle() }
                
                // Ignore this for accessibility - the switch will handle it
                Accessible.ignored: true
            }

            // Optional help link under the label
            Text {
                id: helpText
                Layout.alignment: Qt.AlignVCenter
                visible: pill.helpLabel !== "" && pill.helpUrl !== ""
                text: pill.helpLabel
                font.family: Style.fontFamily
                font.pixelSize: Style.fontSizeDescription
                color: Style.buttonForegroundColor
                font.underline: helpHover.hovered
                Accessible.name: text
                TapHandler {
                    cursorShape: Qt.PointingHandCursor
                    onTapped: Qt.openUrlExternally(pill.helpUrl)
                }
                HoverHandler {
                    id: helpHover
                    acceptedDevices: PointerDevice.Mouse
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }

        // Flexible spacer to push the switch flush-right and align across rows
        Item { Layout.fillWidth: true }

        // Native switch on the right with custom focus styling
        Switch {
            id: sw
            Layout.alignment: Qt.AlignVCenter
            Material.accent: sw.activeFocus ? Style.raspberryRed : Style.formControlActiveColor
            checked: pill.checked
            activeFocusOnTab: true
            
            // Accessibility properties - combine label text with description
            Accessible.role: Accessible.CheckBox
            Accessible.name: {
                var name = label.text
                var desc = ""
                if (pill.accessibleDescription !== "") {
                    desc = pill.accessibleDescription
                } else if (pill.helpLabel !== "") {
                    desc = pill.helpLabel
                }
                // Combine name and description since VoiceOver reads name more reliably
                return desc !== "" ? (name + ", " + desc) : name
            }
            Accessible.description: ""
            Accessible.checkable: true
            Accessible.checked: pill.checked
            Accessible.onToggleAction: toggle()
            
            onToggled: {
                pill.checked = checked
                pill.toggled(checked)
            }
            
            // Focus styling handled by Material.accent color change only

            Keys.onReturnPressed: toggle()
            Keys.onEnterPressed: toggle()
        }

    }

    function forceActiveFocus() { sw.forceActiveFocus() }
}


