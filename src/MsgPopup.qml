/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
import "qmlcomponents"

import RpiImager

ImPopup {
    id: root
    closePolicy: Popup.CloseOnEscape
    focus: true

    property alias text: msgpopupbody.text
    property bool continueButton: true
    property bool quitButton: false
    property bool yesButton: false
    property bool noButton: false
    property bool rebootButton: false

    height: msgpopupbody.implicitHeight+150

    // Provide implementation for the base popup's navigation functions
    getNextFocusableElement: function(startElement) {
        var focusableItems = [yesButton, noButton, continueButton, quitButton, rebootButton, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })

        if (focusableItems.length === 0) return startElement;

        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[0];

        var nextIndex = (currentIndex + 1) % focusableItems.length;
        return focusableItems[nextIndex];
    }

    getPreviousFocusableElement: function(startElement) {
        var focusableItems = [yesButton, noButton, continueButton, quitButton, rebootButton, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })

        if (focusableItems.length === 0) return startElement;

        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[0];

        var prevIndex = (currentIndex - 1 + focusableItems.length) % focusableItems.length;
        return focusableItems[prevIndex];
    }

    // These children go into ImPopup's ColumnLayout

    Text {
        id: msgpopupbody
        font.pointSize: 12
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Style.fontFamily
        Layout.fillHeight: true
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        Layout.topMargin: 10
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Accessible.name: text.replace(/<\/?[^>]+(>|$)/g, "")
    }

    RowLayout {
        Layout.alignment: Qt.AlignCenter | Qt.AlignBottom
        Layout.bottomMargin: 10
        spacing: 20
        id: buttons

        ImButton {
            id: noButton
            text: qsTr("NO")
            onClicked: {
                root.close()
                root.no()
            }
            visible: root.noButton
        }

        ImButtonRed {
            id: yesButton
            text: qsTr("YES")
            onClicked: {
                root.close()
                root.yes()
            }
            visible: root.yesButton
        }

        ImButtonRed {
            id: continueButton
            text: qsTr("CONTINUE")
            onClicked: {
                root.close()
            }
            visible: root.continueButton
        }

        ImButtonRed {
            id: rebootButton
            text: qsTr("REBOOT")
            onClicked: {
                root.close()
                root.reboot()
            }
            visible: root.rebootButton
        }

        ImButtonRed {
            id: quitButton
            text: qsTr("QUIT")
            onClicked: {
                Qt.quit()
            }
            font.family: Style.fontFamily
            visible: root.quitButton
        }
    }


    onOpened: {
        // The popup will get focus by default.
        // First tab press will move focus to the first interactive element.
        root.contentItem.forceActiveFocus()
    }
}
