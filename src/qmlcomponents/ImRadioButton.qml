/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022-2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

RadioButton {
    id: control
    Material.accent: Style.formControlActiveColor
    activeFocusOnTab: true
    
    // Allow custom accessibility description
    property string accessibleDescription: ""
    
    // Export the natural/desired width for dialog sizing calculations
    readonly property real naturalWidth: textMetrics.width + (indicator ? indicator.width : 20) + spacing + leftPadding + rightPadding
    
    // Measure text for naturalWidth (control.font is inherited from RadioButton)
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
    
    // Custom square indicator for embedded mode to avoid circular rendering artifacts
    Component.onCompleted: {
        if (control.imageWriter && control.imageWriter.isEmbeddedMode()) {
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
            radius: 0  // Square instead of circle
            border.color: control.checked ? Style.formControlActiveColor : "#bdbebf"
            border.width: 2
            color: Style.mainBackgroundColor
            
            Rectangle {
                width: 10
                height: 10
                x: 5
                y: 5
                radius: 0  // Square dot instead of circle
                color: Style.formControlActiveColor
                visible: control.checked
            }
        }
    }
    
    // Accessibility properties - combine text with description in name
    Accessible.role: Accessible.RadioButton
    Accessible.name: {
        var name = text
        var desc = accessibleDescription
        return desc !== "" ? (name + ", " + desc) : name
    }
    Accessible.description: ""
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onPressAction: click()
    
    // Add visual focus indicator
    Rectangle {
        anchors.fill: parent
        anchors.margins: Style.focusOutlineMargin
        color: Style.transparent
        border.color: control.activeFocus ? Style.focusOutlineColor : Style.transparent
        border.width: Style.focusOutlineWidth
        radius: (control.imageWriter && control.imageWriter.isEmbeddedMode()) ? 0 : Style.focusOutlineRadius
        z: -1
    }
    
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space) {
            if (!checked)            // prevent unchecking the current one
                click()              // goes through the normal “mouse click” path
            event.accepted = true
        }
    }
    Keys.onEnterPressed: (event) => {
        if (!checked)
            click()
        event.accepted = true
    }

    Keys.onReturnPressed: (event) => {
        if (!checked)
          click()
        event.accepted = true
    }
}
