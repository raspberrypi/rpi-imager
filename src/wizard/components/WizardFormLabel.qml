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
    
    font.pixelSize: Style.fontSizeFormLabel
    font.family: Style.fontFamily
    color: isError ? Style.formLabelErrorColor : 
           isDisabled ? Style.formLabelDisabledColor : 
           Style.formLabelColor
    
    // Default layout properties for consistent spacing
    Layout.fillWidth: false
    Layout.alignment: Qt.AlignVCenter
    
    // Accessibility - by default labels are part of the form flow
    // Individual instances can set Accessible.ignored: false to make them explicitly readable
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Accessible.ignored: true  // Usually read as part of the associated control
} 
