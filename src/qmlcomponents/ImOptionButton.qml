/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RpiImager
import QtQuick.Controls.Material 2.15

// A labeled button styled for Imager; only the button clicks, not the whole row
Item {
    id: control
    property alias text: label.text
    property bool checked: false
    // Optional help link next to the label
    property string helpLabel: ""
    property url helpUrl: ""
    property string btnText: ""
    property alias enabled: optionButton.enabled
    signal clicked()

    // Expose the actual focusable control for tab navigation
    property alias focusItem: optionButton

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
                TapHandler { onTapped: sw.toggle() }
            }

            // Optional help link under the label
            Text {
                id: helpText
                Layout.alignment: Qt.AlignVCenter
                visible: control.helpLabel !== "" && control.helpUrl !== ""
                text: control.helpLabel
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

        ImButton {
            id: optionButton
            Layout.alignment: Qt.AlignVCenter
            activeFocusOnTab: true
            text: btnText

            onClicked: {
                control.clicked()
            }
        }
    }

    function forceActiveFocus() { optionButton.forceActiveFocus() }
}
