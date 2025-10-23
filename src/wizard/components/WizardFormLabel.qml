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
    
    property bool isError: false
    property bool isDisabled: false
    
    font.pixelSize: Style.fontSizeFormLabel
    font.family: Style.fontFamily
    color: isError ? Style.formLabelErrorColor : 
           isDisabled ? Style.formLabelDisabledColor : 
           Style.formLabelColor
    
    // Default layout properties for consistent spacing
    Layout.fillWidth: false
    Layout.alignment: Qt.AlignVCenter
} 
