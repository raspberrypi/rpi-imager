/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Controls.Material 2.2

import RpiImager

ImPopup {
    id: root
    
    property bool userAccepted: false
    
    title: qsTr("Keychain Access")
    
    signal accepted()
    signal rejected()
    
    function askForPermission() {
        userAccepted = false
        open()
    }
    
    ColumnLayout {
        anchors.top: root.title_separator.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 25
        spacing: 20
        
        Text {
            text: qsTr("Would you like to prefill the wifi password from the system keychain?")
            wrapMode: Text.Wrap
            color: Style.textColor
            font.pixelSize: 14
            Layout.fillWidth: true
        }
        
        Text {
            text: qsTr("This will require administrator authentication on macOS.")
            wrapMode: Text.Wrap
            color: Style.textDescriptionColor
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        
        Item {
            Layout.fillHeight: true
        }
        
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            
            Item {
                Layout.fillWidth: true
            }
            
            ImButton {
                text: qsTr("No")
                Layout.preferredWidth: 80
                onClicked: {
                    userAccepted = false
                    root.rejected()
                    root.close()
                }
            }
            
            ImButton {
                text: qsTr("Yes")
                Layout.preferredWidth: 80
                Material.background: Style.buttonBackgroundColor
                onClicked: {
                    userAccepted = true
                    root.accepted()
                    root.close()
                }
            }
        }
    }
    
    onClosed: {
        if (!userAccepted) {
            root.rejected()
        }
    }
}