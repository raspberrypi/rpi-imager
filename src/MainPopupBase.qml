/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

import RpiImager

Popup {
    id: root
    x: 50
    y: 25
    width: parent.width-100
    height: parent.height-50
    padding: 0
    closePolicy: Popup.CloseOnEscape

    required property int windowWidth
    required property string title
    property alias title_separator: title_separator
    property alias closeButton: closeButton

    // Functions to be implemented by derived components
    function getNextFocusableElement(startElement) { return startElement }
    function getPreviousFocusableElement(startElement) { return startElement }

    contentItem: Rectangle {
        id: content
        color: Style.listViewRowBackgroundColor
        Keys.onPressed: (event) => {
            if (!event.accepted) {
                var focusedItem = Window.activeFocusItem
                if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && event.modifiers & Qt.ShiftModifier)) {
                    getPreviousFocusableElement(focusedItem).forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab) {
                    getNextFocusableElement(focusedItem).forceActiveFocus()
                    event.accepted = true
                }
            }
        }
    }

    // background of title
    Rectangle {
        id: title_background
        color: Style.titleBackgroundColor
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

        ImCloseButton {
            id: closeButton
            onClicked: {
                root.close()
            }

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && event.modifiers & Qt.ShiftModifier)) {
                    root.getPreviousFocusableElement(closeButton).forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab) {
                    root.getNextFocusableElement(closeButton).forceActiveFocus()
                    event.accepted = true
                }
            }
        }
    }
    // line under title
    Rectangle {
        id: title_separator
        color: Style.titleSeparatorColor
        width: parent.width
        anchors.top: title_background.bottom
        height: 1
    }
}
