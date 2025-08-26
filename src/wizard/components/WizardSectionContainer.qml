/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RpiImager

Item {
    id: root
    
    default property alias content: childrenLayout.children
    
    Layout.fillWidth: true
    Layout.preferredHeight: childrenLayout.implicitHeight
    Layout.maximumWidth: Style.sectionMaxWidth
    Layout.alignment: Qt.AlignHCenter
    
    ColumnLayout {
        id: childrenLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Style.stepContentSpacing
    }
} 