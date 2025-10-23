/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls

import RpiImager

SelectionListView {
    id: root
    
    // Additional signals for OS-specific navigation
    signal rightPressed(int index, var item, var modelData)
    signal leftPressed()
    
    // Additional properties for OS selection
    property var osSelectionHandler: null
    property var sublistChecker: null
    
    // Track whether the current selection is from keyboard (for auto-advance behavior)
    property bool lastSelectionWasKeyboard: false
    property bool currentSelectionIsFromMouse: false
    
    // Override keyboard navigation to add OS-specific features
    Keys.onRightPressed: {
        if (currentIndex !== -1) {
            var item = itemAtIndex(currentIndex)
            var delegateItem = itemAtIndex(currentIndex)
            var modelData = null
            
            if (delegateItem && delegateItem.model) {
                // C++ model (like OSListModel) - use delegate's model object
                modelData = delegateItem.model
            } else {
                // QML ListModel - fall back to getModelData()
                modelData = getModelData(currentIndex)
            }
            
            root.rightPressed(currentIndex, item, modelData)
        }
    }
    
    Keys.onLeftPressed: function(event) {
        root.leftPressed()
        event.accepted = true
    }
    
    // Override selection to use OS-specific handler if provided
    onItemSelected: function(index, item) {
        if (osSelectionHandler && typeof osSelectionHandler === "function") {
            // Get the delegate item and access its model property (same as mouse click)
            var delegateItem = itemAtIndex(index)
            var modelData = null
            
            if (delegateItem && delegateItem.model) {
                // C++ model (like OSListModel) - use delegate's model object
                modelData = delegateItem.model
            } else {
                // QML ListModel - fall back to getModelData()
                modelData = getModelData(index)
            }
            
            if (modelData) {
                // Check if this is a sublist navigation (we want to allow normal scroll behavior for sublists)
                var isSublist = (typeof(modelData.subitems_json) === "string" && modelData.subitems_json !== "") ||
                                (typeof(modelData.subitems_url) === "string" && modelData.subitems_url !== "" && modelData.subitems_url !== "internal://back")
                var isBackButton = (typeof(modelData.subitems_url) === "string" && modelData.subitems_url === "internal://back")
                
                // If clicking the same item that's already selected, don't try to preserve scroll
                // (this prevents the instability when repeatedly clicking the last item)
                var isReselection = (currentIndex === index)
                
                // Only preserve scroll position for regular OS selection (not sublist navigation or back button or reselection)
                var shouldPreserveScroll = !isSublist && !isBackButton && !isReselection
                
                console.log("OSSelectionListView: index:", index, "currentIndex:", currentIndex, "isReselection:", isReselection, "shouldPreserveScroll:", shouldPreserveScroll, "fromMouse:", currentSelectionIsFromMouse)
                
                // Temporarily disable highlight following AND range mode to prevent ANY scroll on selection
                var wasFollowing = highlightFollowsCurrentItem
                var wasRangeMode = highlightRangeMode
                if (shouldPreserveScroll) {
                    highlightFollowsCurrentItem = false
                    highlightRangeMode = ListView.NoHighlightRange
                }
                
                // Update currentIndex for visual highlight
                currentIndex = index
                
                // NOW save the contentY after setting currentIndex (to capture any scroll that happened despite our disabling)
                // Also capture contentHeight at save time to detect if content size changes
                var savedContentY = shouldPreserveScroll ? contentY : 0
                var savedContentHeight = shouldPreserveScroll ? contentHeight : 0
                
                // Re-enable highlight following and range mode
                if (shouldPreserveScroll) {
                    highlightFollowsCurrentItem = wasFollowing
                    highlightRangeMode = wasRangeMode
                }
                
                // Call the handler with modelData and fromMouse flag
                osSelectionHandler(modelData, !currentSelectionIsFromMouse, currentSelectionIsFromMouse)
                
                // Reset the flag after use
                currentSelectionIsFromMouse = false
                
                // Restore scroll position after all changes
                // Only for regular OS items, not sublists or back button or reselection
                if (shouldPreserveScroll) {
                    Qt.callLater(function() {
                        console.log("OSSelectionListView: Restoring scroll - contentY:", contentY, "savedContentY:", savedContentY, "contentHeight:", contentHeight)
                        // Clamp to contentHeight to ensure we never scroll beyond the content
                        contentY = Math.min(Math.max(0, savedContentY), contentHeight)
                        console.log("OSSelectionListView: Restored to", contentY)
                    })
                }
            }
        }
    }
}
