/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window

import RpiImager

ComboBox {
    id: root
    
    // Access imageWriter from parent context
    property var imageWriter: {
        var item = parent;
        while (item) {
            if (item.imageWriter !== undefined) {
                return item.imageWriter;
            }
            item = item.parent;
        }
        return null;
    }
    
    // Focus properties
    activeFocusOnTab: true
    
    // Accessibility properties
    Accessible.role: Accessible.ComboBox
    Accessible.name: currentText
    Accessible.description: indicateError ? "Error: Invalid selection" : ""
    Accessible.editable: editable
    Accessible.focused: activeFocus
    
    // Enhanced properties
    property bool indicateError: false
    
    // Internal search functionality
    property string searchString: ""
    property real lastKeyTime: 0
    property int lastSearchIndex: -1
    property int originalIndex: -1  // Store original selection when popup opens
    
    // Keyboard navigation settings - more discoverable timeout
    readonly property int searchResetTimeout: 1500  // 1.5 seconds - gives users time to type multiple chars
    readonly property int scrollAnimationDuration: 100  // Smooth animation for scrolling
    
    // Scroll settings for native-like wheel behavior
    readonly property int itemHeight: 40
    readonly property int wheelScrollItems: 3  // Scroll 3 items per wheel notch (native-like)
    
    // Configuration
    selectTextByMouse: true
    editable: false
    
    // Clear search when popup closes
    onPopupChanged: {
        if (!popup.visible) {
            searchString = ""
            lastSearchIndex = -1
        }
    }
    
    function performSearch(inputText) {
        var currentTime = new Date().getTime()
        
        // Reset search after timeout
        if (currentTime - lastKeyTime > searchResetTimeout) {
            searchString = ""
            lastSearchIndex = -1
        }
        lastKeyTime = currentTime
        
        var inputLower = inputText.toLowerCase()
        var previousSearch = searchString
        
        // Check if this is the same single character being pressed repeatedly
        var isSameCharRepeat = (searchString.length === 1 && 
                                searchString === inputLower && 
                                lastSearchIndex >= 0)
        
        if (isSameCharRepeat) {
            // Don't append - just cycle to next match
        } else {
            // Append to build up multi-character search
            searchString += inputLower
        }
        
        // Determine where to start searching
        var startIndex = 0
        if (isSameCharRepeat) {
            // Same character repeated - cycle to next match
            startIndex = (lastSearchIndex + 1) % model.length
        }
        
        // Search for match
        var foundIndex = -1
        var modelLen = model.length
        
        for (var i = 0; i < modelLen; i++) {
            var checkIndex = (startIndex + i) % modelLen
            var text = root.textAt ? root.textAt(checkIndex) : (model[checkIndex] + "")
            if (text.toLowerCase().startsWith(searchString)) {
                foundIndex = checkIndex
                break
            }
        }
        
        // Fallback: if multi-char search failed, try just the new character
        if (foundIndex === -1 && searchString.length > 1) {
            searchString = inputLower
            for (var j = 0; j < modelLen; j++) {
                var text2 = root.textAt ? root.textAt(j) : (model[j] + "")
                if (text2.toLowerCase().startsWith(searchString)) {
                    foundIndex = j
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
                
                // Smooth scroll to center the found item
                var targetY = (foundIndex * itemHeight) - (listView.height / 2) + (itemHeight / 2)
                var maxY = Math.max(0, listView.contentHeight - listView.height)
                var finalY = Math.max(0, Math.min(targetY, maxY))
                
                scrollAnimation.to = finalY
                scrollAnimation.restart()
            }
            return true
        }
        
        return false
    }
    
    function handleBackspace() {
        var currentTime = new Date().getTime()
        
        // Reset search after timeout
        if (currentTime - lastKeyTime > searchResetTimeout) {
            searchString = ""
            lastSearchIndex = -1
            return
        }
        lastKeyTime = currentTime
        
        // Remove last character from search string
        if (searchString.length > 0) {
            searchString = searchString.substring(0, searchString.length - 1)
        } else {
            // Already empty, nothing to do
            return
        }
        
        // Re-search with the shortened string
        if (searchString.length === 0) {
            // Empty search - reset to first item
            lastSearchIndex = -1
            if (model.length > 0) {
                currentIndex = 0
                if (popup.visible) {
                    var listView = popup.contentItem
                    listView.currentIndex = 0
                    scrollAnimation.to = 0
                    scrollAnimation.restart()
                }
            }
            return
        }
        
        // Search for match with shortened string
        var foundIndex = -1
        var modelLen = model.length
        
        // Start from beginning when backspacing
        for (var i = 0; i < modelLen; i++) {
            var text = root.textAt ? root.textAt(i) : (model[i] + "")
            if (text.toLowerCase().startsWith(searchString)) {
                foundIndex = i
                break
            }
        }
        
        if (foundIndex !== -1) {
            lastSearchIndex = foundIndex
            currentIndex = foundIndex
            
            if (popup.visible) {
                var listView = popup.contentItem
                listView.currentIndex = foundIndex
                
                // Smooth scroll to center the found item
                var targetY = (foundIndex * itemHeight) - (listView.height / 2) + (itemHeight / 2)
                var maxY = Math.max(0, listView.contentHeight - listView.height)
                var finalY = Math.max(0, Math.min(targetY, maxY))
                
                scrollAnimation.to = finalY
                scrollAnimation.restart()
            }
        }
    }
    
    // Smooth scroll animation for keyboard navigation
    NumberAnimation {
        id: scrollAnimation
        target: popup.contentItem
        property: "contentY"
        duration: scrollAnimationDuration
        easing.type: Easing.OutCubic
    }
    
    // Custom delegate with key handling for dropdown
    delegate: ItemDelegate {
        id: delegateRoot
        required property var modelData
        required property int index
        
        width: parent ? parent.width : 0
        height: root.itemHeight
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
            // Handle Escape - restore original selection and close popup
            if (event.key === Qt.Key_Escape) {
                root.currentIndex = root.originalIndex
                popupComponent.close()
                event.accepted = true
            }
            // Handle Enter/Return - select current highlighted item and close popup
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                // Use dropdownList.currentIndex which follows the visual highlight
                // (root.highlightedIndex is read-only and not updated by search)
                if (dropdownList.currentIndex >= 0 && dropdownList.currentIndex < root.model.length) {
                    root.currentIndex = dropdownList.currentIndex
                    // Emit activated signal so onActivated handlers run (same as clicking)
                    root.activated(dropdownList.currentIndex)
                }
                popupComponent.close()
                event.accepted = true
            }
            // Handle backspace to delete last character
            else if (event.key === Qt.Key_Backspace) {
                root.handleBackspace()
                event.accepted = true
            }
            // Handle Tab - let it propagate for normal navigation
            else if (event.key === Qt.Key_Tab) {
                // Don't accept - let Tab navigate normally
                event.accepted = false
            }
            // Handle regular character input
            else if (event.text && event.text.length === 1) {
                root.performSearch(event.text)
                event.accepted = true
            }
        }
    }
    
    // Note: Key handling for open popup is left to the focused items inside the popup
    
    // Handle Enter/Return to open popup when ComboBox is focused (but popup closed)
    Keys.onReturnPressed: (event) => {
        if (!popup.visible) {
            popup.open()
            event.accepted = true
        }
    }
    Keys.onEnterPressed: (event) => {
        if (!popup.visible) {
            popup.open()
            event.accepted = true
        }
    }
    
    // Improve mouse wheel behavior: step selection without opening the popup
    WheelHandler {
        id: wheelHandler
        enabled: root.enabled
        target: null
        onWheel: (event) => {
            if (!root.popup.visible) {
                var dy = event.pixelDelta && Math.abs(event.pixelDelta.y) > 0 ? event.pixelDelta.y : event.angleDelta.y
                
                // Qt provides deltas that match the system scroll direction (natural vs traditional).
                // However, for combo boxes (discrete item selection), users expect consistent behavior:
                // "scroll down gesture" should always mean "next item" regardless of scroll preferences.
                // 
                // When event.inverted is true (natural scrolling), Qt's deltas are content-oriented,
                // which feels backwards for discrete selection. We invert to match user expectations.
                if (event.inverted) {
                    dy = -dy
                }
                
                // After adjustment:
                // Negative delta = scroll down = next item (increase index)
                // Positive delta = scroll up = previous item (decrease index)
                if (dy < 0 && root.currentIndex < root.model.length - 1) {
                    root.currentIndex++
                } else if (dy > 0 && root.currentIndex > 0) {
                    root.currentIndex--
                }
                event.accepted = true
            }
        }
    }

    // Provide a default popup with smooth scrolling when the list is open
    popup: Popup {
        id: popupComponent
        padding: 0
        y: root.height
        width: root.width
        height: Math.min(300, Math.max(150, contentItem.implicitHeight))
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        
        // Clear search string when popup opens or closes
        onVisibleChanged: {
            if (visible) {
                // Store original selection when popup opens
                root.originalIndex = root.currentIndex
                root.searchString = ""
                root.lastSearchIndex = -1
                // Ensure ListView receives focus when popup opens for keyboard search
                if (contentItem) {
                    contentItem.forceActiveFocus()
                }
            } else {
                // Clear search when popup closes
                root.searchString = ""
                root.lastSearchIndex = -1
            }
        }
        
        background: Rectangle {
            color: Style.mainBackgroundColor
            radius: (root.imageWriter && root.imageWriter.isEmbeddedMode()) ? Style.sectionBorderRadiusEmbedded : Style.sectionBorderRadius
            border.color: Style.popupBorderColor
            border.width: Style.sectionBorderWidth
            antialiasing: true  // Smooth edges at non-integer scale factors
            clip: true  // Prevent content overflow at non-integer scale factors
            
            // Search indicator overlay - shows what user is typing
            Rectangle {
                id: searchIndicator
                visible: root.searchString.length > 0
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Style.sectionBorderWidth
                height: visible ? 28 : 0
                color: Style.buttonFocusedBackgroundColor
                z: 10
                
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Type to search: \"%1\"").arg(root.searchString)
                    font.pixelSize: Style.fontSizeSmall
                    font.italic: true
                    color: Style.textDescriptionColor
                }
                
                // Hint about cycling with same letter
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: Style.spacingTiny
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.searchString.length === 1
                    text: qsTr("(press again to cycle)")
                    font.pixelSize: Style.fontSizeSmall - 2
                    font.italic: true
                    color: Style.textMetadataColor
                }
            }
        }
        
        contentItem: ListView {
            id: dropdownList
            clip: true
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            delegate: root.delegate
            boundsBehavior: Flickable.StopAtBounds  // Stop at bounds for native feel
            
            // Enable focus so keyboard input works for search
            focus: popupComponent.visible
            
            // Disable flickable behavior - we'll handle scrolling ourselves for smooth control
            interactive: true
            flickDeceleration: 3000  // Standard deceleration
            maximumFlickVelocity: 2500  // Standard velocity limit
            
            preferredHighlightBegin: 0
            preferredHighlightEnd: height
            highlightRangeMode: ListView.ApplyRange
            
            // Handle keyboard input for search when popup is open
            Keys.onPressed: (event) => {
                // Handle Escape - restore original selection and close popup
                if (event.key === Qt.Key_Escape) {
                    root.currentIndex = root.originalIndex
                    popupComponent.close()
                    event.accepted = true
                }
                // Handle Enter/Return - select current highlighted item and close popup
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    // Use dropdownList.currentIndex which follows the visual highlight
                    // (root.highlightedIndex is read-only and not updated by search)
                    if (dropdownList.currentIndex >= 0 && dropdownList.currentIndex < root.model.length) {
                        root.currentIndex = dropdownList.currentIndex
                        // Emit activated signal so onActivated handlers run (same as clicking)
                        root.activated(dropdownList.currentIndex)
                    }
                    popupComponent.close()
                    event.accepted = true
                }
                // Handle backspace to delete last character
                else if (event.key === Qt.Key_Backspace) {
                    root.handleBackspace()
                    event.accepted = true
                }
                // Handle Tab - let it propagate for normal navigation
                else if (event.key === Qt.Key_Tab) {
                    // Don't accept - let Tab navigate normally
                    event.accepted = false
                }
                // Handle regular character input
                else if (event.text && event.text.length === 1) {
                    root.performSearch(event.text)
                    event.accepted = true
                }
            }
            
            // Smooth highlight animations
            highlightMoveDuration: 100
            highlightResizeDuration: 100
            add: null
            remove: null
            move: null
            displaced: null
            
            // Custom highlight that respects popup border radius
            highlight: Rectangle {
                color: Style.listViewHighlightColor
                radius: (root.imageWriter && root.imageWriter.isEmbeddedMode()) ? Style.sectionBorderRadiusEmbedded : Style.sectionBorderRadius
                border.color: dropdownList.activeFocus ? Style.buttonFocusedBackgroundColor : "transparent"
                border.width: dropdownList.activeFocus ? 2 : 0
                antialiasing: true  // Smooth edges at non-integer scale factors
                
                // Add consistent margins to keep highlight within popup's rounded borders
                anchors.margins: 2
                anchors.rightMargin: 2 + (dropdownList.contentHeight > dropdownList.height ? Style.scrollBarWidth : 0)
            }
            
            ScrollBar.vertical: ScrollBar {
                policy: dropdownList.contentHeight > dropdownList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                width: Style.scrollBarWidth
            }
            
            // Smooth scroll animation for wheel events
            NumberAnimation {
                id: listScrollAnimation
                target: dropdownList
                property: "contentY"
                duration: 150
                easing.type: Easing.OutCubic
            }
            
            // Native-like wheel scrolling: smooth, proportional, no flicking
            WheelHandler {
                onWheel: (event) => {
                    // Stop any ongoing animation
                    listScrollAnimation.stop()
                    
                    // Get scroll delta - prefer pixel delta for smooth trackpads
                    var dy
                    if (event.pixelDelta && Math.abs(event.pixelDelta.y) > 0) {
                        // Trackpad or precision scroll - use pixel values directly
                        dy = event.pixelDelta.y
                    } else {
                        // Mouse wheel - convert angle to pixels
                        // Standard: 120 units = 1 notch = 3 lines of scrolling
                        var notches = event.angleDelta.y / 120.0
                        dy = notches * root.itemHeight * root.wheelScrollItems
                    }
                    
                    // Handle OS scroll direction preference (natural vs traditional)
                    if (PlatformHelper.isScrollInverted(event.inverted)) {
                        dy = -dy
                    }
                    
                    // Calculate new position with bounds checking
                    var newY = dropdownList.contentY + dy
                    var maxY = Math.max(0, dropdownList.contentHeight - dropdownList.height)
                    newY = Math.max(0, Math.min(newY, maxY))
                    
                    // Animate to new position for smoothness
                    listScrollAnimation.to = newY
                    listScrollAnimation.restart()
                    
                    event.accepted = true
                }
            }
        }
    }
}
