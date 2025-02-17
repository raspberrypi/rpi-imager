
/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Layouts 1.0

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

    MouseArea {
        anchors.fill: parent
        onClicked: {
            root.clicked();
        }
    }
}
