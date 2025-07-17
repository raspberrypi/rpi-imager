/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
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