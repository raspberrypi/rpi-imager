/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import RpiImager

CheckBox {
    id: control
    Material.accent: Style.formControlActiveColor
    font.pointSize: Style.fontSizeSm
    font.family: Style.fontFamily
    activeFocusOnTab: true
    focusPolicy: Qt.TabFocus
    
    // Export the natural/desired width for dialog sizing calculations
    readonly property real naturalWidth: textMetrics.width + (indicator ? indicator.width : 20) + spacing + leftPadding + rightPadding
    
    // Measure text for naturalWidth (control.font is inherited from CheckBox)
    TextMetrics {
        id: textMetrics
        font: control.font
        text: control.text
    }
    
    // Custom contentItem with text wrapping for long translations
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Style.formLabelColor : Style.formLabelDisabledColor
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator ? (control.indicator.width + control.spacing) : 0
        wrapMode: Text.WordWrap
        width: control.availableWidth  // Constrain width so text wraps
    }
    
    // Custom square indicator for embedded mode to avoid rendering artifacts
    Component.onCompleted: {
        if (ImageWriterSingleton && ImageWriterSingleton.isEmbeddedMode()) {
            control.indicator = squareIndicatorComponent.createObject(control)
        }
    }
    
    Component {
        id: squareIndicatorComponent
        Rectangle {
            implicitWidth: 20
            implicitHeight: 20
            x: control.leftPadding
            y: control.height / 2 - height / 2
            radius: 0  // Square checkbox
            border.color: control.checked ? Style.formControlActiveColor : "#bdbebf"
            border.width: 2
            color: control.checked ? Style.formControlActiveColor : Style.mainBackgroundColor
            
            // Checkmark
            Text {
                anchors.centerIn: parent
                text: "✓"
                color: Style.mainBackgroundColor
                font.pointSize: Style.fontSizeSm
                font.bold: true
                visible: control.checked
            }
        }
    }
    
    // Accessibility properties
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: checked
    // AbstractButton::toggle() flips `checked` and stops there -- toggled()
    // belongs to the click path and is not emitted. Every keyboard route here
    // used to call it, so the box changed on screen while nothing listening
    // for a deliberate change was told.
    //
    // The storage step is why that matters: "Exclude system drives" raises its
    // confirmation from onToggled, so unchecking it with the keyboard listed
    // the user's system drives with no confirmation at all. Space was broken
    // too -- the handler below shadows the native Space handling that would
    // otherwise have gone through the click path.
    function activate() {
        control.checked = !control.checked
        control.toggled()
    }

    Accessible.onToggleAction: control.activate()

    Keys.onEnterPressed: control.activate()
    Keys.onReturnPressed: control.activate()
    Keys.onSpacePressed: control.activate()
    
    Rectangle {
        // This rectangle serves as a high-contrast underline for focus
        anchors.left: control.contentItem.left
        anchors.right: control.contentItem.right
        anchors.top: control.contentItem.bottom
        anchors.topMargin: 2
        height: 2
        color: Style.button2FocusedBackgroundColor
        visible: control.activeFocus
    }
}
