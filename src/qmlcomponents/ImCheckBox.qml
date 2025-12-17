/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import RpiImager

CheckBox {
    id: control
    Material.accent: Style.formControlActiveColor
    activeFocusOnTab: true
    
    // Export the natural/desired width for dialog sizing calculations
    readonly property real naturalWidth: textMetrics.width + (indicator ? indicator.width : 20) + spacing + leftPadding + rightPadding
    
    // Measure text for naturalWidth (control.font is inherited from CheckBox)
    TextMetrics {
        id: textMetrics
        font: control.font
        text: control.text
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
            radius: 0  // Square checkbox
            border.color: control.checked ? Style.formControlActiveColor : "#bdbebf"
            border.width: 2
            color: control.checked ? Style.formControlActiveColor : Style.mainBackgroundColor
            
            // Checkmark
            Text {
                anchors.centerIn: parent
                text: "✓"
                color: Style.mainBackgroundColor
                font.pixelSize: 14
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
    Accessible.onToggleAction: toggle()

    Keys.onEnterPressed: toggle()
    Keys.onReturnPressed: toggle()
    Keys.onSpacePressed: toggle()
    
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
