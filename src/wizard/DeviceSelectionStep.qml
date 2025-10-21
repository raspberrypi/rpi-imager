/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    readonly property HWListModel hwModel: imageWriter.getHWList()
    
    title: qsTr("Select your Raspberry Pi device")
    showNextButton: true
    nextButtonEnabled: false  // Disabled until device is selected or auto-selected
    
    property alias hwlist: hwlist
    property bool modelLoaded: false
    property bool hasDeviceSelected: false
    
    // Forward the nextClicked signal as next() function for keyboard auto-advance
    function next() {
        root.nextClicked()
    }
    
    Component.onCompleted: {
        // Initial load only
        if (!modelLoaded) onOsListPreparedHandler()
        
        // Register the ListView for keyboard navigation
        root.registerFocusGroup("device_list", function(){
            return [hwlist]
        }, 0)
        
        // Set the initial focus item to the ListView
        root.initialFocusItem = hwlist
        
        // Ensure focus starts on the device list when entering this step (same as OSSelectionStep)
        hwlist.forceActiveFocus()
    }

    Connections {
        target: imageWriter
        function onOsListPrepared() {
            onOsListPreparedHandler()
        }
    }
    
    // Called when OS list data is ready from network
    function onOsListPreparedHandler() {
        if (!root || !root.hwModel) {
            return
        }

        // Don't guard with modelLoaded to support reloading when repo changed
        console.log("DeviceSelectionStep: OS list prepared, reloading hardware model")
        var success = root.hwModel.reload()
        if (success) {
            modelLoaded = true
            // Set initial focus for keyboard navigation if no device is selected
            if (hwlist.currentIndex === -1 && root.hwModel.rowCount() > 0) {
                hwlist.currentIndex = 0
            }
        }
    }
    
    // Content
    content: [
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Device list (fills available space)
        SelectionListView {
            id: hwlist
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.hwModel
            delegate: hwdelegate
            autoSelectFirst: true
            keyboardAutoAdvance: true
            nextFunction: root.next
            accessibleName: qsTr("Device selection list. Select a Raspberry Pi device. Use arrow keys to navigate, Enter or Space to select")
            accessibleDescription: ""
            
            Component.onCompleted: {
                if (root.hwModel && root.hwModel.currentIndex !== undefined && root.hwModel.currentIndex >= 0) {
                    currentIndex = root.hwModel.currentIndex
                    root.hasDeviceSelected = true
                    root.nextButtonEnabled = true
                } else {
                    // Delay auto-selection slightly to allow VoiceOver to announce the list container first
                    Qt.callLater(function() {
                        if (currentIndex === -1 && count > 0) {
                            currentIndex = 0
                        }
                    })
                }
            }
            
            onCurrentIndexChanged: {
                root.hasDeviceSelected = currentIndex !== -1
                root.nextButtonEnabled = root.hasDeviceSelected
            }
            
            onItemSelected: function(index, item) {
                if (index >= 0 && index < model.rowCount()) {
                    // Set the model's current index (this triggers the HWListModel logic)
                    root.hwModel.currentIndex = index
                    // Use the model's currentName property
                    root.wizardContainer.selectedDeviceName = root.hwModel.currentName
                    root.hasDeviceSelected = true
                    root.nextButtonEnabled = true
                }
            }
        }
    }
    ]
    
    // Device delegate component
    Component {
        id: hwdelegate
        
        Item {
            id: hwitem
            
            required property int index
            required property string name
            required property string description
            required property string icon
            required property QtObject model
            
            width: hwlist.width
            // Let content determine height for balanced vertical padding
            height: Math.max(60, row.implicitHeight + Style.spacingSmall + Style.spacingMedium)
            
            // Accessibility properties
            Accessible.role: Accessible.ListItem
            Accessible.name: hwitem.name
            Accessible.description: hwitem.description
            Accessible.focusable: true
            Accessible.ignored: false
            
            Rectangle {
                id: hwbgrect
                anchors.fill: parent
                color: (hwlist.currentIndex === hwitem.index) ? Style.listViewHighlightColor :
                       (hwMouseArea.containsMouse ? Style.listViewHoverRowBackgroundColor : Style.listViewRowBackgroundColor)
                radius: 0
                anchors.rightMargin: (hwlist.contentHeight > hwlist.height ? Style.scrollBarWidth : 0)
                Accessible.ignored: true
                
                MouseArea {
                    id: hwMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    
                    onClicked: {
                        hwlist.currentIndex = hwitem.index
                        root.hwModel.currentIndex = hwitem.index
                        root.wizardContainer.selectedDeviceName = hwitem.name

                        // Enable Next; do not auto-advance
                        root.hasDeviceSelected = true
                        root.nextButtonEnabled = true
                    }
                }
                
                RowLayout {
                    id: row
                    anchors.fill: parent
                    anchors.leftMargin: Style.listItemPadding
                    anchors.rightMargin: Style.listItemPadding
                    anchors.topMargin: Style.spacingSmall
                    anchors.bottomMargin: Style.spacingMedium
                    spacing: Style.spacingMedium
                    
                    // Hardware Icon
                    Image {
                        id: hwicon
                        source: hwitem.icon || ""
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        // Rasterize vector sources at device pixel ratio to avoid aliasing/blurriness on HiDPI
                        sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio), Math.round(height * Screen.devicePixelRatio))
                        visible: source.toString().length > 0
                        
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: Style.titleSeparatorColor
                            border.width: 1
                            radius: 0
                            visible: parent.status === Image.Error
                        }
                    }
                    
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.spacingXXSmall
                        
                        Text {
                            text: hwitem.name
                            font.pixelSize: Style.fontSizeFormLabel
                            font.family: Style.fontFamilyBold
                            font.bold: true
                            color: Style.formLabelColor
                            Layout.fillWidth: true
                        }
                        
                        Text {
                            text: hwitem.description
                            font.pixelSize: Style.fontSizeDescription
                            font.family: Style.fontFamily
                            color: Style.textDescriptionColor
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
} 
