/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Layouts
import RpiImager

Text {
    id: root
    
    property bool isError: false
    property bool isDisabled: false
    
    // When set, label becomes independently focusable for screen readers with this description
    property string accessibleDescription: ""
    
    // Access imageWriter from ancestor context
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
    
    font.pixelSize: Style.fontSizeFormLabel
    font.family: Style.fontFamily
    color: isError ? Style.formLabelErrorColor : 
           isDisabled ? Style.formLabelDisabledColor : 
           Style.formLabelColor
    
    // Default layout properties for consistent spacing
    Layout.fillWidth: false
    Layout.alignment: Qt.AlignVCenter
    
    // Accessibility - labels become keyboard-focusable when screen reader is active
    // When accessibleDescription is set, label is independently focusable (not ignored)
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Accessible.description: accessibleDescription
    Accessible.ignored: accessibleDescription.length === 0
    Accessible.focusable: imageWriter ? imageWriter.isScreenReaderActive() : false
    focusPolicy: (imageWriter && imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
    activeFocusOnTab: imageWriter ? imageWriter.isScreenReaderActive() : false
} 
