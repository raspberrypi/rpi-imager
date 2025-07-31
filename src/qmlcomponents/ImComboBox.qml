/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2

ComboBox {
    id: root
    
    // Enhanced properties
    property bool indicateError: false
    
    // Internal search functionality
    property string searchString: ""
    property real lastKeyTime: 0
    
    // Configuration
    selectTextByMouse: true
    editable: false
    
    function performSearch(inputText) {
        var currentTime = new Date().getTime()
        
        // Reset search string if too much time has passed
        if (currentTime - lastKeyTime > 1000) {
            searchString = ""
        }
        lastKeyTime = currentTime
        
        searchString += inputText.toLowerCase()
        
        // Find matching item
        for (var i = 0; i < model.length; i++) {
            if (model[i].toLowerCase().startsWith(searchString)) {
                currentIndex = i
                if (popup.visible) {
                    popup.contentItem.currentIndex = i
                    popup.contentItem.positionViewAtIndex(i, ListView.Center)
                }
                return true
            }
        }
        return false
    }
    
    // Custom delegate with key handling for dropdown
    delegate: ItemDelegate {
        width: parent ? parent.width : 0
        height: 40
        
        contentItem: Text {
            text: modelData
            font: root.font
            verticalAlignment: Text.AlignVCenter
            leftPadding: 12
            color: root.indicateError ? "#cc0000" : "#666666"
        }
        
        highlighted: root.highlightedIndex === index
        
        Keys.onPressed: (event) => {
            if (event.text && event.text.length === 1) {
                root.performSearch(event.text)
                event.accepted = true
            }
        }
    }
    
    // Handle keyboard input when ComboBox is closed
    Keys.onPressed: (event) => {
        // Don't handle tab navigation here - let parent handle it
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            return // Let parent handle navigation
        }
        
        // Handle search when ComboBox is closed
        if (event.text && event.text.length === 1 && !popup.visible) {
            performSearch(event.text)
            event.accepted = true
            return
        }
    }
    
    // Ensure popup has proper key handling when dropdown is open
    Component.onCompleted: {
        if (popup && popup.contentItem) {
            popup.contentItem.Keys.pressed.connect(function(event) {
                if (event.text && event.text.length === 1) {
                    performSearch(event.text)
                    event.accepted = true
                }
            })
        }
    }
    
    // Improve mouse wheel behavior
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: (wheel) => {
            if (enabled && !popup.visible) {
                // Smooth scrolling - only change by 1 item per wheel event
                if (wheel.angleDelta.y > 0 && parent.currentIndex > 0) {
                    parent.currentIndex--
                } else if (wheel.angleDelta.y < 0 && parent.currentIndex < parent.model.length - 1) {
                    parent.currentIndex++
                }
            }
            wheel.accepted = true
        }
    }
}