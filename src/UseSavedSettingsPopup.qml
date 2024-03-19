/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
import "qmlcomponents"

Popup {
    id: msgpopup
    x: (parent.width-width)/2
    y: (parent.height-height)/2
    width: Math.max(buttons.width+10, parent.width-150)
    height: msgpopupbody.implicitHeight+150
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    modal: true
        
    background: Rectangle {
        color: UnColors.darkGray
        border.color: UnColors.mediumGray
    }

    property bool hasSavedSettings: false

    signal yes()
    signal no()
    signal noClearSettings()
    signal editSettings()
    signal closeSettings()

    // background of title
    Rectangle {
        color: UnColors.mediumGray
        anchors.right: parent.right
        anchors.top: parent.top
        height: 35
        width: parent.width
    }
    // line under title
    Rectangle {
        color: UnColors.mediumGray
        width: parent.width
        y: 35
        implicitHeight: 1
    }

    Text {
        id: msgx
        text: "X"
        color: "white"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 25
        anchors.topMargin: 10
        font.family: roboto.name
        font.bold: true

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                msgpopup.close()
            }
        }
    }

    ColumnLayout {
        spacing: 20
        anchors.fill: parent

        Text {
            id: msgpopupheader
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
            Layout.topMargin: 10
            font.family: roboto.name
            font.bold: true
            text: qsTr("Use OS customization?")
            color: "white"
        }

        Text {
            id: msgpopupbody
            font.pointSize: 12
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            font.family: roboto.name
            Layout.maximumWidth: msgpopup.width-50
            Layout.fillHeight: true
            Layout.leftMargin: 25
            Layout.topMargin: 25
            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            Accessible.name: text.replace(/<\/?[^>]+(>|$)/g, "")
            text: qsTr("Would you like to apply OS customization settings?")
            color: "white"
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter | Qt.AlignBottom
            Layout.bottomMargin: 10
            spacing: 20
            id: buttons

            ImButton {
                text: qsTr("EDIT SETTINGS")
                onClicked: {
                    // Don't close this dialog when "edit settings" is
                    // clicked, as we don't want the user to fall back to the
                    // start of the flow. After editing the settings we want
                    // then to once again have the choice about whether to use
                    // customisation or not.
                    msgpopup.editSettings()
                }
            }

            ImButton {
                id: noAndClearButton
                text: qsTr("NO, CLEAR SETTINGS")
                onClicked: {
                    msgpopup.close()
                    msgpopup.noClearSettings()
                }
                enabled: hasSavedSettings
            }

            ImButton {
                id: yesButton
                text: qsTr("YES")
                onClicked: {
                    msgpopup.close()
                    msgpopup.yes()
                }
                enabled: hasSavedSettings
            }

            ImButton {
                text: qsTr("NO")
                onClicked: {
                    msgpopup.close()
                    msgpopup.no()
                }
            }

            Text { text: " " }
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
