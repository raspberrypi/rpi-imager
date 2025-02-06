/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

Popup {
    id: root
    x: 50
    y: 25
    width: parent.width-100
    height: parent.height-50
    padding: 0
    closePolicy: Popup.CloseOnEscape

    required property string title
    property alias title_separator: title_separator

    // background of title
    Rectangle {
        id: title_background
        color: "#f5f5f5"
        anchors.left: parent.left
        anchors.top: parent.top
        height: 35
        width: parent.width

        Text {
            text: root.title
            horizontalAlignment: Text.AlignHCenter
            anchors.fill: parent
            anchors.topMargin: 10
            font.family: Style.fontFamily
            font.bold: true
        }

        Text {
            text: "X"
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
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.close()
                }
            }
        }
    }
    // line under title
    Rectangle {
        id: title_separator
        color: "#afafaf"
        width: parent.width
        anchors.top: title_background.bottom
        height: 1
    }
}
