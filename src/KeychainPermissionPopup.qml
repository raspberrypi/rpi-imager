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
        root.userAccepted = false
        open()
    }
    
    Text {
        text: qsTr("Would you like to prefill the wifi password from the system keychain?")
        wrapMode: Text.Wrap
        color: Style.textDescriptionColor
        font.pixelSize: 14
        Layout.fillWidth: true
        Layout.leftMargin: 25
        Layout.rightMargin: 25
        Layout.topMargin: 25
    }
    
    Text {
        text: qsTr("This will require administrator authentication on macOS.")
        wrapMode: Text.Wrap
        color: Style.textMetadataColor
        font.pixelSize: 12
        Layout.fillWidth: true
        Layout.leftMargin: 25
        Layout.rightMargin: 25
    }
    
    Item {
        Layout.fillHeight: true
    }
    
    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 10
        spacing: 10
        
        Item {
            Layout.fillWidth: true
        }
        
        ImButton {
            text: qsTr("No")
            Layout.preferredWidth: 80
            onClicked: {
                root.userAccepted = false
                root.rejected()
                root.close()
            }
        }
        
        ImButton {
            text: qsTr("Yes")
            Layout.preferredWidth: 80
            Material.background: Style.buttonBackgroundColor
            onClicked: {
                root.userAccepted = true
                root.accepted()
                root.close()
            }
        }
    }
    
    onClosed: {
        if (!root.userAccepted) {
            root.rejected()
        }
    }
}