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
    
    property bool isError: false
    property bool isDisabled: false
    
    // When set, label becomes independently focusable for screen readers with this description
    property string accessibleDescription: ""
    
    font.pointSize: Style.fontSizeFormLabel
    font.family: Style.fontFamily
    color: isError ? Style.formLabelErrorColor : 
           isDisabled ? Style.formLabelDisabledColor : 
           Style.formLabelColor
    
    // Default layout properties for consistent spacing
    Layout.fillWidth: false
    Layout.alignment: Qt.AlignVCenter
    
    // Accessibility - labels become keyboard-focusable when screen reader is active
    // When accessibleDescription is set, label is independently focusable (not ignored)
    Accessible.description: accessibleDescription
    Accessible.ignored: accessibleDescription.length === 0
} 
