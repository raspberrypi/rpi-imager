/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

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

    // Opening the help link, in one place. The pointer, the three keyboard
    // routes and the accessibility press action all arrive here, so no route
    // can quietly drift from the others or be left off a new one. A test
    // shadows this method to see which routes arrive, rather than launching a
    // browser on the machine running the suite.
    function openHelpLink() {
        if (ImageWriterSingleton) {
            ImageWriterSingleton.openUrl(pill.helpUrl)
        } else {
            Qt.openUrlExternally(pill.helpUrl)
        }
    }
    
    // Single source of truth for label font (used by both label and TextMetrics)
    readonly property font labelFont: Qt.font({
        family: Style.fontFamilyBold,
        pointSize: Style.fontSizeFormLabel,
        bold: true
    })
    
    // Export the natural/desired width for dialog sizing calculations
    // This is independent of Layout.fillWidth constraints
    readonly property real naturalWidth: labelMetrics.width + sw.implicitWidth + Style.spacingMedium * 2 + Style.cardPadding

    // Measure label text independently for naturalWidth
    TextMetrics {
        id: labelMetrics
        font: pill.labelFont
        text: pill.text
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
            Layout.alignment: Qt.AlignVCenter
            // Constrain width so text elides properly, leaving room for switch
            Layout.maximumWidth: pill.width - sw.implicitWidth - Style.spacingMedium * 2
            spacing: Style.spacingXXSmall

            // Main label
            Text {
                id: label
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                font: pill.labelFont
                color: Style.formLabelColor
                elide: Text.ElideRight
                TapHandler { onTapped: sw.toggle() }
                
                // Ignore this for accessibility - the switch will handle it
                Accessible.ignored: true
            }

            // Optional help link under the label
            Text {
                id: helpText
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                // See ImOptionButton: a url property is a JS object in Qt 6,
                // so a strict comparison against "" is always true.
                visible: pill.helpLabel !== "" && String(pill.helpUrl) !== ""
                text: pill.helpLabel
                font.family: Style.fontFamily
                font.pointSize: Style.fontSizeDescription
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
                    onTapped: { pill.openHelpLink() }
                }
                HoverHandler {
                    id: helpHover
                    acceptedDevices: PointerDevice.Mouse
                    cursorShape: Qt.PointingHandCursor
                }
                
                // Keyboard activation
                Keys.onEnterPressed: { pill.openHelpLink() }
                Keys.onReturnPressed: { pill.openHelpLink() }
                Keys.onSpacePressed: { pill.openHelpLink() }
                
                Accessible.onPressAction: { pill.openHelpLink() }
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
            focusPolicy: Qt.TabFocus
            
            // Custom rectangular indicator for embedded mode to avoid circular rendering artifacts
            Component.onCompleted: {
                if (ImageWriterSingleton && ImageWriterSingleton.isEmbeddedMode()) {
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
                            NumberAnimation { duration: PlatformHelper.prefersReducedMotion ? 0 : 100 }
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
            Accessible.onToggleAction: pill.activate()
            
            onToggled: {
                pill.checked = checked
                pill.toggled(checked)
            }
            
            // Focus styling handled by Material.accent color change only

            // Not toggle(): it moves the switch without emitting toggled(),
            // so the step is never told and pill.checked stays behind what
            // is drawn. See ImCheckBox for the same mistake and its cost.
            Keys.onReturnPressed: pill.activate()
            Keys.onEnterPressed: pill.activate()
        }

    }

    function forceActiveFocus() { sw.forceActiveFocus() }

    // Flip the pill the way a click does: sw.checked is bound to this, so the
    // switch follows, and the step hears about it.
    function activate() {
        pill.checked = !pill.checked
        pill.toggled(pill.checked)
    }
}

