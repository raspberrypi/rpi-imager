/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    readonly property HWListModel hwModel: imageWriter.getHWList()
    
    title: qsTr("Select your Raspberry Pi device")
    showNextButton: true
    // Enable Next when device selected OR when offline (so users can proceed with custom image)
    nextButtonEnabled: hasDeviceSelected || (fetchFailed && hwlist.count === 0)
    
    property alias hwlist: hwlist
    property bool modelLoaded: false
    property bool hasDeviceSelected: false
    property bool isReloadingModel: false
    
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
        
        // Initial focus will automatically go to title, then first control (handled by WizardStepBase)
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

        // Only reload if we haven't loaded yet, to avoid resetting scroll position during device selection
        if (!modelLoaded) {
            isReloadingModel = true
            var success = root.hwModel.reload()
            if (success) {
                modelLoaded = true
                // Do not auto-select first item to avoid unwanted highlighting on load
            }
            isReloadingModel = false
        }
    }
    
    // Track whether OS list fetch failed
    readonly property bool fetchFailed: imageWriter.isOsListFetchFailed
    
    // Content
    content: [
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Offline/fetch failed placeholder (shown when list is empty due to network failure)
        Item {
            id: offlinePlaceholder
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: hwlist.count === 0 && root.fetchFailed
            
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width * 0.8
                spacing: Style.spacingLarge
                
                // Icon or visual indicator
                Text {
                    text: "⚠"
                    font.pixelSize: 48
                    color: Style.textDescriptionColor
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.ignored: true
                }
                
                Text {
                    text: qsTr("Unable to load device list")
                    font.pixelSize: Style.fontSizeHeading
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                    Accessible.ignored: false
                }
                
                Text {
                    text: qsTr("The device list could not be downloaded. Please check your internet connection and try again.\n\nYou can still write a local image file by pressing Next and selecting 'Use custom' on the following screen.")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.textDescriptionColor
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    lineHeight: 1.3
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                    Accessible.ignored: false
                }
                
                ImButton {
                    id: retryButton
                    text: qsTr("Retry")
                    Layout.alignment: Qt.AlignHCenter
                    accessibleDescription: qsTr("Retry downloading the device list")
                    onClicked: {
                        imageWriter.beginOSListFetch()
                    }
                }
            }
        }

        // Device list (fills available space, hidden when showing offline placeholder)
        SelectionListView {
            id: hwlist
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !offlinePlaceholder.visible
            model: root.hwModel
            delegate: hwdelegate
            keyboardAutoAdvance: true
            nextFunction: root.next
            accessibleName: {
                var count = hwlist.count
                var name = qsTr("Device selection list")
                
                if (count === 0) {
                    name += ". " + qsTr("No devices")
                } else if (count === 1) {
                    name += ". " + qsTr("1 device")
                } else {
                    name += ". " + qsTr("%1 devices").arg(count)
                }
                
                name += ". " + qsTr("Use arrow keys to navigate, Enter or Space to select")
                return name
            }
            accessibleDescription: ""
            
            Component.onCompleted: {
                if (root.hwModel && root.hwModel.currentIndex !== undefined && root.hwModel.currentIndex >= 0) {
                    currentIndex = root.hwModel.currentIndex
                    root.hasDeviceSelected = true
                }
                // Do not auto-select first item to avoid unwanted highlighting on load
            }
            
            onCurrentIndexChanged: {
                root.hasDeviceSelected = currentIndex !== -1
            }
            
            onItemSelected: function(index, item) {
                if (index >= 0 && index < model.rowCount()) {
                    // Only save/restore scroll position if we're not reloading the model
                    // (During model reload, the list may have changed and we should start at top)
                    var shouldPreserveScroll = !root.isReloadingModel
                    var savedContentY = shouldPreserveScroll ? contentY : 0
                    
                    // Update ListView's currentIndex (for visual highlight)
                    currentIndex = index
                    
                    // Set the model's current index (this triggers the HWListModel logic)
                    root.hwModel.currentIndex = index
                    // Use the model's currentName property
                    root.wizardContainer.selectedDeviceName = root.hwModel.currentName
                    root.hasDeviceSelected = true
                    
                    // Restore scroll position after all changes (clamped to valid range)
                    if (shouldPreserveScroll) {
                        Qt.callLater(function() {
                            // Clamp to valid range: 0 to (contentHeight - height)
                            var maxContentY = Math.max(0, contentHeight - height)
                            contentY = Math.min(Math.max(0, savedContentY), maxContentY)
                        })
                    }
                }
            }
            
            onItemDoubleClicked: function(index, item) {
                // First select the item
                if (index >= 0 && index < model.rowCount()) {
                    currentIndex = index
                    root.hwModel.currentIndex = index
                    root.wizardContainer.selectedDeviceName = root.hwModel.currentName
                    root.hasDeviceSelected = true
                    
                    // Then advance to next step (same as pressing Return)
                    Qt.callLater(function() {
                        if (root.nextButtonEnabled) {
                            root.next()
                        }
                    })
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
            Accessible.name: hwitem.name + ". " + hwitem.description
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
                        // Trigger the itemSelected signal by setting ListView's currentIndex
                        // This will handle all the selection logic in onItemSelected
                        hwlist.itemSelected(hwitem.index, hwitem)
                    }
                    
                    onDoubleClicked: {
                        // Double-click acts like pressing Return - select and advance
                        hwlist.itemDoubleClicked(hwitem.index, hwitem)
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
                        sourceSize: Qt.size(Math.round(Layout.preferredWidth * Screen.devicePixelRatio), Math.round(Layout.preferredHeight * Screen.devicePixelRatio))
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
                            Accessible.ignored: true
                        }
                        
                        Text {
                            text: hwitem.description
                            font.pixelSize: Style.fontSizeDescription
                            font.family: Style.fontFamily
                            color: Style.textDescriptionColor
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }
                }
            }
        }
    }
} 
