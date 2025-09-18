/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Window 2.15

import RpiImager

ComboBox {
    id: root
    
    // Focus properties
    activeFocusOnTab: true
    
    // Enhanced properties
    property bool indicateError: false
    // Wheel scrolling configuration (no inertia for closed state)
    property bool wheelInvert: false
    
    // Internal search functionality
    property string searchString: ""
    property real lastKeyTime: 0
    property int lastSearchIndex: -1
    
    // Ultra-fast keyboard navigation settings
    readonly property int searchResetTimeout: 300  // Very fast reset timeout
    readonly property int scrollAnimationDuration: 30  // Very fast but smooth animation
    readonly property double keyboardSpeedMultiplier: 50.0  // 50x faster than default
    
    // Configuration
    selectTextByMouse: true
    editable: false
    
    function performSearch(inputText) {
        var currentTime = new Date().getTime()
        
        // Ultra-fast reset timeout for rapid typing
        if (currentTime - lastKeyTime > searchResetTimeout) {
            searchString = ""
            lastSearchIndex = -1
        }
        lastKeyTime = currentTime
        
        var inputLower = inputText.toLowerCase()
        
        // Always prioritize single character searches for speed
        // If user types rapidly, reset to just the latest character
        if (currentTime - lastKeyTime < 50) { // Very rapid typing
            searchString = inputLower
        } else {
            searchString += inputLower
        }
        
        // Start search from the next item if we're repeating the same character
        var startIndex = 0
        if (searchString.length === 1 && searchString === inputLower && lastSearchIndex >= 0) {
            // Same character repeated - instantly jump to next match
            startIndex = (lastSearchIndex + 1) % model.length
        }
        
        // Ultra-fast search - find first match immediately
        var foundIndex = -1
        var modelLen = model.length
        
        // Primary search with accumulated string
        for (var i = 0; i < modelLen; i++) {
            var checkIndex = (startIndex + i) % modelLen
            var text = root.textAt ? root.textAt(checkIndex) : (model[checkIndex] + "")
            if (text.toLowerCase().startsWith(searchString)) {
                foundIndex = checkIndex
                break
            }
        }
        
        // Fallback: if no match, use just the new character
        if (foundIndex === -1 && searchString !== inputLower) {
            searchString = inputLower
            for (var j = 0; j < modelLen; j++) {
                var checkIndex2 = (startIndex + j) % modelLen
                var text2 = root.textAt ? root.textAt(checkIndex2) : (model[checkIndex2] + "")
                if (text2.toLowerCase().startsWith(searchString)) {
                    foundIndex = checkIndex2
                    break
                }
            }
        }
        
        if (foundIndex !== -1) {
            lastSearchIndex = foundIndex
            currentIndex = foundIndex
            
            if (popup.visible) {
                var listView = popup.contentItem
                listView.currentIndex = foundIndex
                
                // Fast but smooth positioning with 30ms animation
                var itemHeight = 40
                var targetY = (foundIndex * itemHeight) - (listView.height / 2)
                var maxY = Math.max(0, listView.contentHeight - listView.height)
                var finalY = Math.max(0, Math.min(targetY, maxY))
                
                // Create a quick animation for smooth scrolling
                if (!listView.scrollAnimation) {
                    listView.scrollAnimation = Qt.createQmlObject(`
                        import QtQuick 2.15
                        NumberAnimation {
                            target: parent
                            property: "contentY"
                            duration: 30
                            easing.type: Easing.OutQuad
                        }
                    `, listView, "scrollAnimation")
                }
                
                listView.scrollAnimation.to = finalY
                listView.scrollAnimation.restart()
            }
            return true
        }
        
        return false
    }
    
    // Custom delegate with key handling for dropdown
    delegate: ItemDelegate {
        id: delegateRoot
        required property var modelData
        required property int index
        
        width: parent ? parent.width : 0
        height: 40
        padding: 0
        leftPadding: Style.spacingTiny
        rightPadding: Style.spacingTiny
        topPadding: 0
        bottomPadding: 0

        contentItem: Text {
            text: delegateRoot.modelData
            font: root.font
            verticalAlignment: Text.AlignVCenter
            color: root.indicateError ? Style.formLabelErrorColor : Style.textDescriptionColor
        }
        
        highlighted: root.highlightedIndex === delegateRoot.index
        
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
            // Ultra-fast scrolling - no deceleration limits
            flickDeceleration: 100000  // Instant stop
            maximumFlickVelocity: 500000  // Extremely fast velocity
            preferredHighlightBegin: 0
            preferredHighlightEnd: height
            highlightRangeMode: ListView.ApplyRange
            
            // Very fast animations for smooth but responsive feel
            highlightMoveDuration: 30
            highlightResizeDuration: 30
            add: null
            remove: null
            move: null
            displaced: null
            
            // Custom highlight that respects popup border radius
            highlight: Rectangle {
                color: Style.listViewHighlightColor
                radius: Style.sectionBorderRadius
                border.color: dropdownList.activeFocus ? Style.buttonFocusedBackgroundColor : "transparent"
                border.width: dropdownList.activeFocus ? 2 : 0
                
                // Add consistent margins to keep highlight within popup's rounded borders
                anchors.margins: 2
                anchors.rightMargin: 2 + (dropdownList.contentHeight > dropdownList.height ? Style.scrollBarWidth : 0)
            }
            
            ScrollBar.vertical: ScrollBar {
                policy: dropdownList.contentHeight > dropdownList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                width: Style.scrollBarWidth
            }
            // Wheel inertia inside the popup: convert wheel delta to a flick velocity (OS-aware)
            WheelHandler {
                onWheel: (event) => {
                    var dy = event.pixelDelta && Math.abs(event.pixelDelta.y) > 0 ? event.pixelDelta.y : event.angleDelta.y
                    // Respect OS natural scrolling preference
                    if (event.inverted) dy = -dy
                    // Scale velocity based on OS wheel scroll lines setting
                    var baseVelocity = -dy * 10
                    var osAwareVelocity = baseVelocity * root.keyboardSpeedMultiplier
                    dropdownList.flick(0, osAwareVelocity)
                    event.accepted = true
                }
            }
        }
    }
}