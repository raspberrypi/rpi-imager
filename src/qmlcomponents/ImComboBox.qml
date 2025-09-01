/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Window 2.15

ComboBox {
    id: root
    
    // Enhanced properties
    property bool indicateError: false
    // Wheel scrolling configuration (no inertia for closed state)
    property bool wheelInvert: false
    
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
            var text = root.textAt ? root.textAt(i) : (model[i] + "")
            if (text.toLowerCase().startsWith(searchString)) {
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
        padding: 0
        leftPadding: Style.spacingTiny
        rightPadding: Style.spacingTiny
        topPadding: 0
        bottomPadding: 0

        contentItem: Text {
            text: modelData
            font: root.font
            verticalAlignment: Text.AlignVCenter
            color: root.indicateError ? Style.formLabelErrorColor : Style.textDescriptionColor
        }
        
        highlighted: root.highlightedIndex === index
        
        Keys.onPressed: (event) => {
            if (event.text && event.text.length === 1) {
                root.performSearch(event.text)
                event.accepted = true
            }
        }
    }
    
    // Note: Avoid global key handlers here to prevent interfering with TextFields elsewhere
    
    // Note: Key handling for open popup is left to the focused items inside the popup
    
    // Improve mouse wheel behavior: step selection without opening the popup (no inertia)
    WheelHandler {
        id: wheelHandler
        enabled: root.enabled
        target: null
        onWheel: (event) => {
            if (!root.popup.visible) {
                var dy = event.pixelDelta && Math.abs(event.pixelDelta.y) > 0 ? event.pixelDelta.y : event.angleDelta.y
                // Respect OS natural scrolling preference
                if (event.inverted) dy = -dy
                if (root.wheelInvert) dy = -dy
                if (dy > 0 && root.currentIndex > 0) {
                    root.currentIndex--
                } else if (dy < 0 && root.currentIndex < root.model.length - 1) {
                    root.currentIndex++
                }
                event.accepted = true
            }
        }
    }

    // Provide a default popup with inertial scrolling when the list is open
    popup: Popup {
        padding: 0
        y: root.height
        width: root.width
        height: Math.min(300, Math.max(150, contentItem.implicitHeight))
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        background: Rectangle {
            color: Style.mainBackgroundColor
            radius: Style.sectionBorderRadius
            border.color: Style.popupBorderColor
            border.width: Style.sectionBorderWidth
        }
        contentItem: ListView {
            id: dropdownList
            clip: true
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            delegate: root.delegate
            boundsBehavior: Flickable.DragAndOvershootBounds
            flickDeceleration: 2500
            maximumFlickVelocity: 5000
            preferredHighlightBegin: 0
            preferredHighlightEnd: height
            highlightRangeMode: ListView.ApplyRange
            ScrollBar.vertical: ScrollBar {
                policy: dropdownList.contentHeight > dropdownList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                width: Style.scrollBarWidth
            }
            // Wheel inertia inside the popup: convert wheel delta to a flick velocity
            WheelHandler {
                onWheel: (event) => {
                    var dy = event.pixelDelta && Math.abs(event.pixelDelta.y) > 0 ? event.pixelDelta.y : event.angleDelta.y
                    // Respect OS natural scrolling preference
                    if (event.inverted) dy = -dy
                    // Scale to a reasonable velocity; negative dy scrolls down
                    var velocity = -dy * 10
                    dropdownList.flick(0, velocity)
                    event.accepted = true
                }
            }
        }
    }
}