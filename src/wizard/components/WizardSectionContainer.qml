/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RpiImager

Rectangle {
    id: root
    
    default property alias content: childrenLayout.children
    
    Layout.fillWidth: true
    Layout.preferredHeight: childrenLayout.implicitHeight + (2 * Style.sectionMargins)
    Layout.maximumWidth: Style.sectionMaxWidth
    Layout.alignment: Qt.AlignHCenter
    
    color: Style.titleBackgroundColor
    border.color: Style.titleSeparatorColor
    border.width: Style.sectionBorderWidth
    radius: Style.sectionBorderRadius
    
    ColumnLayout {
        id: childrenLayout
        anchors.fill: parent
        anchors.margins: Style.sectionMargins
        spacing: Style.stepContentSpacing
    }
} 