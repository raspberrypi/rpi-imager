/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    readonly property HWListModel hwModel: imageWriter.getHWList()
    
    title: qsTr("Choose Device")
    subtitle: qsTr("Select your Raspberry Pi device to filter and optimize the available images")
    showNextButton: false
    
    property alias hwlist: hwlist
    property bool modelLoaded: false
    
    // Forward the nextClicked signal as next() function
    function next() {
        root.nextClicked()
    }
    
    Component.onCompleted: {
        // Connect to osListPrepared signal to reload models when data is ready
        imageWriter.osListPrepared.connect(onOsListPrepared)
        
        // Try initial load in case data is already available
        onOsListPrepared()
    }
    
    // Called when OS list data is ready from network
    function onOsListPrepared() {
        // Only reload if we haven't loaded yet or if the model is empty
        if (!modelLoaded || root.hwModel.rowCount() === 0) {
            console.log("DeviceSelectionStep: OS list prepared, reloading hardware model")
            var success = root.hwModel.reload()
            if (success) {
                modelLoaded = true
            }
        }
    }
    
    // Content
    ColumnLayout {
        anchors.fill: parent
        spacing: 20
        
        // Device selection area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Style.titleBackgroundColor
            border.color: Style.titleSeparatorColor
            border.width: 1
            radius: 8
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15
                
                Text {
                    text: qsTr("Select your Raspberry Pi device:")
                    font.pixelSize: 16
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
                
                // Device list
                ListView {
                    id: hwlist
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.hwModel
                    currentIndex: -1
                    
                    Component.onCompleted: {
                        if (root.hwModel.defaultIndex !== undefined) {
                            currentIndex = root.hwModel.defaultIndex
                        }
                    }
                    delegate: hwdelegate
                    clip: true
                    
                    boundsBehavior: Flickable.StopAtBounds
                    highlight: Rectangle { 
                        color: Style.listViewHighlightColor
                        radius: 5 
                    }
                    
                    ScrollBar.vertical: ScrollBar {
                        width: 10
                        policy: hwlist.contentHeight > hwlist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                    }
                }
            }
        }
        

    }
    
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
            height: 60
            
            Rectangle {
                id: hwbgrect
                anchors.fill: parent
                color: hwMouseArea.containsMouse ? Style.listViewHoverRowBackgroundColor : 
                       (hwlist.currentIndex === index ? Style.listViewHighlightColor : Style.listViewRowBackgroundColor)
                radius: 5
                
                MouseArea {
                    id: hwMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    
                    onClicked: {
                        hwlist.currentIndex = index
                        root.hwModel.currentIndex = index
                        root.wizardContainer.selectedDeviceName = hwitem.name
                        // Auto-advance to next step
                        root.next()
                    }
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    // Hardware Icon
                    Image {
                        source: hwitem.icon || ""
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        fillMode: Image.PreserveAspectFit
                        visible: source.toString().length > 0
                        
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: Style.titleSeparatorColor
                            border.width: 1
                            radius: 4
                            visible: parent.status === Image.Error
                        }
                    }
                    
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        
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
                            font.pixelSize: Style.fontSizeInput
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