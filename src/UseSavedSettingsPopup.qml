/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
import "qmlcomponents"

import RpiImager

ImPopup {
    id: root

    height: msgpopupbody.implicitHeight+150
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    focus: true

    required property ImageWriter imageWriter
    property bool hasSavedSettings: false

    signal noClearSettings()
    signal editSettings()
    signal closeSettings()

    // Provide implementation for the base popup's navigation functions
    getNextFocusableElement: function(startElement) {
        var focusableItems = [yesButton, noButton, editButton, noAndClearButton, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })

        if (focusableItems.length === 0) return startElement;
        
        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[0];

        var nextIndex = (currentIndex + 1) % focusableItems.length;
        return focusableItems[nextIndex];
    }

    getPreviousFocusableElement: function(startElement) {
        var focusableItems = [yesButton, noButton, editButton, noAndClearButton, root.closeButton].filter(function(item) {
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
        Layout.leftMargin: 25
        Layout.rightMargin: 25
        Layout.topMargin: 25
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        Accessible.name: text.replace(/<\/?[^>]+(>|$)/g, "")
        text: qsTr("Would you like to apply OS customization settings?")
    }

    RowLayout {
        Layout.alignment: Qt.AlignCenter | Qt.AlignBottom
        Layout.bottomMargin: 10
        id: buttons

        ImButtonRed {
            id: editButton
            text: qsTr("EDIT SETTINGS")
            onClicked: {
                // Don't close this dialog when "edit settings" is
                // clicked, as we don't want the user to fall back to the
                // start of the flow. After editing the settings we want
                // then to once again have the choice about whether to use
                // customisation or not.
                root.editSettings()
            }
        }

        ImButtonRed {
            id: noAndClearButton
            text: qsTr("NO, CLEAR SETTINGS")
            onClicked: {
                root.close()
                root.noClearSettings()
            }
            enabled: root.hasSavedSettings
        }

        ImButtonRed {
            id: yesButton
            text: qsTr("YES")
            onClicked: {
                root.close()
                root.yes()
            }
            enabled: root.hasSavedSettings
        }

        ImButtonRed {
            id: noButton
            text: qsTr("NO")
            onClicked: {
                root.close()
                root.no()
            }
        }
    }

    onOpened: {
        hasSavedSettings = imageWriter.hasSavedCustomizationSettings()
        root.contentItem.forceActiveFocus()
    }

    onClosed: {
        // Close the advanced options window if this msgbox is dismissed,
        // in order to prevent the user from starting writing while the
        // advanced options window is open.
        closeSettings()
    }
}
