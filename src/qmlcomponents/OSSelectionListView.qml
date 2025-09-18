/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15

import RpiImager

SelectionListView {
    id: root
    
    // Additional signals for OS-specific navigation
    signal rightPressed(int index, var item, var modelData)
    signal leftPressed()
    
    // Additional properties for OS selection
    property var osSelectionHandler: null
    property var sublistChecker: null
    
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
    
    Keys.onLeftPressed: {
        root.leftPressed()
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
                osSelectionHandler(modelData)
            }
        }
    }
}
