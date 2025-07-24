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
    property alias closeButton: closeButton

    // Functions to be implemented by derived components
    property var getNextFocusableElement: function(startElement) { return startElement }
    property var getPreviousFocusableElement: function(startElement) { return startElement }

    signal yes()
    signal no()
    signal reboot()

    contentItem: Item {
        focus: true
        Keys.onPressed: (event) => {
            if (!event.accepted) {
                var focusedItem = Window.activeFocusItem
                if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && event.modifiers & Qt.ShiftModifier)) {
                    msgpopup.getPreviousFocusableElement(focusedItem).forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab) {
                    msgpopup.getNextFocusableElement(focusedItem).forceActiveFocus()
                    event.accepted = true
                }
            }
        }
    }

    onOpened: {
        contentItem.forceActiveFocus()
    }

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
            id: closeButton
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

        // Derived components inject their children here.
    }
}
