/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Layouts
import RpiImager

Text {
    id: root
    
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
    
    font.pixelSize: Style.fontSizeDescription
    font.family: Style.fontFamily
    color: Style.textDescriptionColor
    
    // Default layout properties for consistent spacing
    Layout.fillWidth: true
    wrapMode: Text.WordWrap
    
    // Accessibility - description text becomes keyboard-focusable when screen reader is active
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Accessible.ignored: false
    Accessible.focusable: imageWriter ? imageWriter.isScreenReaderActive() : false
    focusPolicy: (imageWriter && imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
    activeFocusOnTab: imageWriter ? imageWriter.isScreenReaderActive() : false
} 
