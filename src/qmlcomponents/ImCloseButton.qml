
/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Layouts

import RpiImager

Text {
    id: root
    signal clicked

    text: "X"

    // Layouting is here as all usage sites use the same code, so we save some lines.
    // Move it out usage once different usage is needed
    Layout.alignment: Qt.AlignRight
    horizontalAlignment: Text.AlignRight
    verticalAlignment: Text.AlignVCenter
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.rightMargin: 25
    anchors.topMargin: 10

    font.family: Style.fontFamily
    font.bold: true
    
    // Accessibility properties
    Accessible.role: Accessible.Button
    Accessible.name: "Close"
    Accessible.description: "Close dialog"
    Accessible.onPressAction: root.clicked()
    
    // Make it keyboard accessible
    focus: true
    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    MouseArea {
        anchors.fill: parent
        onClicked: {
            root.clicked();
        }
    }
}
