/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2

import RpiImager

Popup {
    id: msgpopup
    x: (parent.width-width)/2
    y: (parent.height-height)/2
    width: 550
    padding: 0
    closePolicy: Popup.CloseOnEscape
    modal: true

    default property alias content: contents.data

    property alias title: msgpopupheader.text

    signal yes()
    signal no()

    // background of title
    Rectangle {
        id: msgpopup_title_background
        color: Style.titleBackgroundColor
        anchors.left: parent.left
        anchors.top: parent.top
        height: 35
        width: parent.width

        Text {
            id: msgpopupheader
            horizontalAlignment: Text.AlignHCenter
            anchors.fill: parent
            anchors.topMargin: 10
            font.family: Style.fontFamily
            font.bold: true
        }

        ImCloseButton {
            onClicked: {
                msgpopup.close();
            }
        }
    }
    // line under title
    Rectangle {
        id: msgpopup_title_separator
        color: Style.titleSeparatorColor
        width: parent.width
        anchors.top: msgpopup_title_background.bottom
        height: 1
    }

    ColumnLayout {
        id: contents
        spacing: 20
        anchors.top: msgpopup_title_separator.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        // Derived components inject their childrens here.
    }
}
