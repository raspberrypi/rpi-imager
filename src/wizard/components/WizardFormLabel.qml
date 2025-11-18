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
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Accessible.ignored: true  // Usually read as part of the associated control
    Accessible.focusable: imageWriter ? imageWriter.isScreenReaderActive() : false
    focusPolicy: (imageWriter && imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
    activeFocusOnTab: imageWriter ? imageWriter.isScreenReaderActive() : false
} 
