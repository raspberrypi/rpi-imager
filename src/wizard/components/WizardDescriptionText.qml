/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RpiImager

Text {
    id: root
    
    font.pixelSize: Style.fontSizeDescription
    font.family: Style.fontFamily
    color: Style.textDescriptionColor
    
    // Default layout properties for consistent spacing
    Layout.fillWidth: true
    wrapMode: Text.WordWrap
} 
