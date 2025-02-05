/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
import "qmlcomponents"

ImPopup {
    id: root

    height: msgpopupbody.implicitHeight+150
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property bool hasSavedSettings: false

    signal noClearSettings()
    signal editSettings()
    signal closeSettings()

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
            text: qsTr("NO")
            onClicked: {
                root.close()
                root.no()
            }
        }
    }

    function openPopup() {
        open()
        if (imageWriter.hasSavedCustomizationSettings()) {
            /* HACK: Bizarrely, the button enabled characteristics are not re-evaluated on open.
             * So, let's manually _force_ these buttons to be enabled */
            hasSavedSettings = true
        }

        // trigger screen reader to speak out message
        msgpopupbody.forceActiveFocus()
    }

    onClosed: {
        // Close the advanced options window if this msgbox is dismissed,
        // in order to prevent the user from starting writing while the
        // advanced options window is open.
        closeSettings()
    }
}
