/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import RpiImager

ListView {
    id: root
    
    // Properties that can be customized
    property bool autoSelectFirst: false
    property bool keyboardAutoAdvance: false
    property var nextFunction: null
    property var isItemSelectableFunction: null  // Function(index) that returns true if item can be selected
    property string accessibleName: "Selection list"
    property string accessibleDescription: "Use arrow keys to navigate, Enter or Space to select"
    
    // Signals for selection actions
    signal itemSelected(int index, var item)
    signal itemDoubleClicked(int index, var item)
    signal spacePressed(int index, var item)
    signal enterPressed(int index, var item) 
    signal returnPressed(int index, var item)
    
    // Helper function for keyboard auto-advance
    function handleKeyboardSelection(index, item) {
        // Always call the itemSelected signal first
        root.itemSelected(index, item)
        
        // If auto-advance is enabled and we have a next function, call it
        if (root.keyboardAutoAdvance && root.nextFunction && typeof root.nextFunction === "function") {
            Qt.callLater(function() {
                root.nextFunction()
            })
        }
    }
    
    // Standard ListView configuration for selection lists
    clip: true
    focus: true
    activeFocusOnTab: true
    boundsBehavior: Flickable.StopAtBounds
    currentIndex: -1
    
    // Accessibility properties
    Accessible.role: Accessible.List
    Accessible.name: root.accessibleName
    Accessible.description: root.accessibleDescription
    
    // Standard highlight configuration
    highlight: Rectangle {
        // When focused: use stronger highlight color
        // When not focused (default selected): use subtle highlight to show it's the default choice
        color: root.activeFocus ? Style.listViewHighlightColor : Style.listViewRowBackgroundColor
        radius: 0
        anchors.fill: parent
        anchors.rightMargin: (root.contentHeight > root.height ? Style.scrollBarWidth : 0)
    }
    highlightFollowsCurrentItem: true
    highlightRangeMode: ListView.ApplyRange
    preferredHighlightBegin: 0
    preferredHighlightEnd: height
    
    // Standard ScrollBar
    ScrollBar.vertical: ScrollBar {
        width: Style.scrollBarWidth
        policy: root.contentHeight > root.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    }
    
    // Focus management
    onActiveFocusChanged: {
        if (activeFocus && currentIndex === -1 && count > 0 && autoSelectFirst) {
            // Delay selection to allow VoiceOver to announce the list container first
            Qt.callLater(function() {
                if (activeFocus && currentIndex === -1 && count > 0) {
                    currentIndex = 0
                }
            })
        }
    }
    
    // Ensure we have a selection when count changes
    onCountChanged: {
        if (count > 0 && currentIndex === -1 && autoSelectFirst) {
            // Delay selection to allow VoiceOver to announce the list container first
            Qt.callLater(function() {
                if (count > 0 && currentIndex === -1) {
                    currentIndex = 0
                }
            })
        }
    }
    
    // Helper function to check if an item is selectable
    function isItemSelectable(index) {
        if (isItemSelectableFunction && typeof isItemSelectableFunction === "function") {
            return isItemSelectableFunction(index)
        }
        return true  // By default, all items are selectable
    }
    
    // Helper function to find next selectable item in a direction
    function findNextSelectableIndex(fromIndex, direction) {
        var nextIndex = fromIndex + direction
        var maxIterations = count  // Prevent infinite loops
        var iterations = 0
        
        while (iterations < maxIterations) {
            // Check bounds
            if (nextIndex < 0 || nextIndex >= count) {
                return fromIndex  // Stay at current position if we hit the edge
            }
            
            // Check if this index is selectable
            if (isItemSelectable(nextIndex)) {
                return nextIndex
            }
            
            // Move to next index in the direction
            nextIndex += direction
            iterations++
        }
        
        // If no selectable item found, stay at current position
        return fromIndex
    }
    
    // Standard keyboard navigation
    Keys.onUpPressed: {
        // Initialize selection if needed
        if (currentIndex === -1 && count > 0) {
            // Find first selectable item from the top
            var firstSelectable = findNextSelectableIndex(-1, 1)
            if (firstSelectable !== -1) {
                currentIndex = firstSelectable
            }
        } else if (currentIndex > 0) {
            var newIndex = findNextSelectableIndex(currentIndex, -1)
            if (newIndex !== currentIndex) {
                currentIndex = newIndex
                positionViewAtIndex(currentIndex, ListView.Center)
            }
        }
    }
    
    Keys.onDownPressed: {
        // Initialize selection if needed
        if (currentIndex === -1 && count > 0) {
            // Find first selectable item from the top
            var firstSelectable = findNextSelectableIndex(-1, 1)
            if (firstSelectable !== -1) {
                currentIndex = firstSelectable
            }
        } else if (currentIndex < count - 1) {
            var newIndex = findNextSelectableIndex(currentIndex, 1)
            if (newIndex !== currentIndex) {
                currentIndex = newIndex
                positionViewAtIndex(currentIndex, ListView.Center)
            }
        }
    }
    
    Keys.onSpacePressed: {
        if (currentIndex !== -1) {
            var item = itemAtIndex(currentIndex)
            root.spacePressed(currentIndex, item)
            root.handleKeyboardSelection(currentIndex, item)
        }
    }
    
    Keys.onEnterPressed: {
        if (currentIndex !== -1) {
            var item = itemAtIndex(currentIndex)
            root.enterPressed(currentIndex, item)
            root.handleKeyboardSelection(currentIndex, item)
        }
    }
    
    Keys.onReturnPressed: {
        if (currentIndex !== -1) {
            var item = itemAtIndex(currentIndex)
            root.returnPressed(currentIndex, item)
            root.handleKeyboardSelection(currentIndex, item)
        }
    }
    
    // Accessibility support
    Accessible.onPressAction: {
        if (currentIndex !== -1) {
            var item = itemAtIndex(currentIndex)
            root.itemSelected(currentIndex, item)
        }
    }
    
    // Helper function to get model data safely
    function getModelData(index) {
        if (!model || index < 0 || index >= count) {
            return null
        }
        
        // For QML ListModel (has get() method)
        if (typeof model.get === "function") {
            return model.get(index)
        }
        
        // For QAbstractListModel (like OSListModel, HWListModel)
        // Create a JavaScript object with all role data
        var modelIndex = model.index(index, 0)
        if (!modelIndex || !modelIndex.valid) {
            return null
        }
        
        var data = {}
        var roles = model.roleNames ? model.roleNames() : {}
        for (var roleKey in roles) {
            var roleName = roles[roleKey]
            var value = model.data(modelIndex, parseInt(roleKey))
            // Provide sensible defaults for undefined values
            if (value === undefined || value === null) {
                if (roleName === "url" || roleName === "icon" || roleName === "subitems_json" || 
                    roleName === "extract_sha256" || roleName === "init_format" || roleName === "release_date" ||
                    roleName === "tooltip" || roleName === "website" || roleName === "architecture") {
                    value = ""
                } else if (roleName === "image_download_size" || roleName === "extract_size") {
                    value = 0
                } else if (roleName === "capabilities") {
                    value = []
                } else if (roleName === "contains_multiple_files" || roleName === "random" || roleName === "enable_rpi_connect") {
                    value = false
                }
            }
            data[roleName] = value
        }
        
        // Add missing properties that might not be in the model but are expected by the code
        if (!("subitems_url" in data)) {
            data.subitems_url = ""
        }
        if (!("contains_multiple_files" in data)) {
            data.contains_multiple_files = false
        }
        
        return data
    }
    
    // Helper function to select an item programmatically
    function selectItem(index) {
        if (index >= 0 && index < count) {
            currentIndex = index
            var item = itemAtIndex(index)
            root.itemSelected(index, item)
        }
    }
}
