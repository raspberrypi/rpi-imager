/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
import "qmlcomponents"

ImPopup {
    id: root
    closePolicy: Popup.CloseOnEscape

    property alias text: msgpopupbody.text
    property bool continueButton: true
    property bool quitButton: false
    property bool yesButton: false
    property bool noButton: false

    height: msgpopupbody.implicitHeight+150

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

        ImButtonRed {
            text: qsTr("NO")
            onClicked: {
                root.close()
                root.no()
            }
            visible: root.noButton
        }

        ImButtonRed {
            text: qsTr("YES")
            onClicked: {
                root.close()
                root.yes()
            }
            visible: root.yesButton
        }

        ImButtonRed {
            text: qsTr("CONTINUE")
            onClicked: {
                root.close()
            }
            visible: root.continueButton
        }

        ImButtonRed {
            text: qsTr("QUIT")
            onClicked: {
                Qt.quit()
            }
            font.family: Style.fontFamily
            visible: root.quitButton
        }
    }


    function openPopup() {
        open()
        // trigger screen reader to speak out message
        msgpopupbody.forceActiveFocus()
    }
}
