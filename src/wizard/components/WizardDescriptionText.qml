/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import RpiImager

FocusableText {
    id: root
    
    font.pointSize: Style.fontSizeDescription
    font.family: Style.fontFamily
    color: Style.textDescriptionColor
    
    // Default layout properties for consistent spacing
    Layout.fillWidth: true
    wrapMode: Text.WordWrap
    
    // Accessibility - description text becomes keyboard-focusable when screen reader is active
    Accessible.ignored: false
} 
