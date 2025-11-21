/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RpiImager
import QtQuick.Controls.Material

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
    // Expose the help link for tab navigation (when visible)
    property alias helpLinkItem: helpText

    // Access imageWriter from parent context
    property var imageWriter: {
        var item = parent;
        while (item) {
            if (item.imageWriter !== undefined) {
                return item.imageWriter;
            }
            item = item.parent;
        }
        return null;
    }

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
                    onTapped: {
                        if (pill.imageWriter) {
                            pill.imageWriter.openUrl(pill.helpUrl)
                        } else {
                            Qt.openUrlExternally(pill.helpUrl)
                        }
                    }
                }
                HoverHandler {
                    id: helpHover
                    acceptedDevices: PointerDevice.Mouse
                    cursorShape: Qt.PointingHandCursor
                }
                
                // Keyboard activation
                Keys.onEnterPressed: {
                    if (pill.imageWriter) {
                        pill.imageWriter.openUrl(pill.helpUrl)
                    } else {
                        Qt.openUrlExternally(pill.helpUrl)
                    }
                }
                Keys.onReturnPressed: {
                    if (pill.imageWriter) {
                        pill.imageWriter.openUrl(pill.helpUrl)
                    } else {
                        Qt.openUrlExternally(pill.helpUrl)
                    }
                }
                Keys.onSpacePressed: {
                    if (pill.imageWriter) {
                        pill.imageWriter.openUrl(pill.helpUrl)
                    } else {
                        Qt.openUrlExternally(pill.helpUrl)
                    }
                }
                
                Accessible.onPressAction: {
                    if (pill.imageWriter) {
                        pill.imageWriter.openUrl(pill.helpUrl)
                    } else {
                        Qt.openUrlExternally(pill.helpUrl)
                    }
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
            
            // Access imageWriter from parent context
            property var imageWriter: {
                var item = parent;
                while (item) {
                    if (item.imageWriter !== undefined) {
                        return item.imageWriter;
                    }
                    item = item.parent;
                }
                return null;
            }
            
            // Custom rectangular indicator for embedded mode to avoid circular rendering artifacts
            Component.onCompleted: {
                if (sw.imageWriter && sw.imageWriter.isEmbeddedMode()) {
                    sw.indicator = squareIndicatorComponent.createObject(sw)
                }
            }
            
            Component {
                id: squareIndicatorComponent
                Rectangle {
                    implicitWidth: 48
                    implicitHeight: 24
                    x: sw.leftPadding
                    y: sw.height / 2 - height / 2
                    radius: 0  // Square track
                    color: sw.checked ? Style.formControlActiveColor : "#bdbebf"
                    border.color: sw.checked ? Style.formControlActiveColor : "#bdbebf"
                    
                    Rectangle {
                        x: sw.checked ? parent.width - width - 2 : 2
                        y: 2
                        width: 20
                        height: 20
                        radius: 0  // Square thumb
                        color: Style.mainBackgroundColor
                        border.color: sw.checked ? Style.formControlActiveColor : "#bdbebf"
                        
                        Behavior on x {
                            NumberAnimation { duration: 100 }
                        }
                    }
                }
            }
            
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


