/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

import RpiImager

ScrollView {
    id: root

    property double scrollPosition

    font.family: Style.fontFamily
    Layout.fillWidth: true
    Layout.fillHeight: true

    // clip: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    // Auto-scroll functionality
    function ensureVisible(item) {
        if (!item || !item.visible || !item.parent) {
            return
        }

        // Wait a frame to ensure all layout calculations are complete
        Qt.callLater(function() { ensureVisibleImmediate(item) })
    }

    function ensureVisibleImmediate(item) {
        if (!item || !item.visible || !item.parent || !contentItem) {
            return
        }

        // Get the actual content container (should be the first child, which is the ColumnLayout)
        var actualContent = contentChildren.length > 0 ? contentChildren[0] : null
        if (!actualContent) {
            return
        }

        // Get the item's position relative to the actual content container
        var itemPos = item.mapToItem(actualContent, 0, 0)
        if (!itemPos) {
            return
        }
        
        var itemY = itemPos.y
        var itemHeight = item.height
        
        // Get the actual content height
        var actualContentHeight = actualContent.height
        
        // Get current scroll position using contentY if available, otherwise ScrollBar
        var currentScrollY = 0
        if (contentItem && contentItem.contentY !== undefined) {
            currentScrollY = contentItem.contentY
        } else {
            var scrollBarPosition = ScrollBar.vertical ? ScrollBar.vertical.position : 0
            var maxScroll = Math.max(0, actualContentHeight - height)
            currentScrollY = scrollBarPosition * maxScroll
        }
        
        var visibleTop = currentScrollY
        var visibleBottom = currentScrollY + height
        
        // Add some padding for better visibility
        var padding = 20
        
        // Check if item is already comfortably visible
        if (itemY >= (visibleTop + padding) && (itemY + itemHeight) <= (visibleBottom - padding)) {
            return // Already comfortably visible
        }
        
        var targetScrollY
        
        // If item is above visible area, scroll to show it at the top
        if (itemY < visibleTop + padding) {
            targetScrollY = Math.max(0, itemY - padding)
        }
        // If item is below visible area, scroll to show it at the bottom
        else if (itemY + itemHeight > visibleBottom - padding) {
            targetScrollY = Math.min(actualContentHeight - height, itemY + itemHeight + padding - height)
        }
        else {
            return
        }
        
        var maxScroll = Math.max(0, actualContentHeight - height)
        if (maxScroll <= 0) {
            return // No scrolling needed
        }
        
        targetScrollY = Math.max(0, Math.min(targetScrollY, maxScroll))
        
        // Try to scroll using contentY if available
        if (contentItem && contentItem.contentY !== undefined) {
            contentScrollAnimation.to = targetScrollY
            contentScrollAnimation.start()
        } else {
            // Fallback to ScrollBar position
            var newScrollPosition = targetScrollY / maxScroll
            scrollAnimation.to = newScrollPosition
            scrollAnimation.start()
        }
    }

    // Recursively find the currently focused item within our content
    function findActiveFocusItem(item) {
        if (!item) return null
        
        if (item.activeFocus) {
            return item
        }
        
        for (var i = 0; i < item.children.length; i++) {
            var child = item.children[i]
            var result = findActiveFocusItem(child)
            if (result) return result
        }
        
        return null
    }

    // Track focus changes using a timer since we can't reliably detect focus changes otherwise
    Timer {
        id: focusCheckTimer
        interval: 100  // Check every 100ms - frequent enough but not too taxing
        running: root.visible
        repeat: true
        
        property Item lastFocusedItem: null
        
        onTriggered: {
            var currentlyFocused = root.findActiveFocusItem(root.contentItem)
            if (currentlyFocused && currentlyFocused !== lastFocusedItem) {
                lastFocusedItem = currentlyFocused
                root.ensureVisible(currentlyFocused)
            }
        }
    }

    // Reset focus tracking when tab becomes invisible
    onVisibleChanged: {
        if (!visible) {
            focusCheckTimer.lastFocusedItem = null
        }
    }

    // Animation for contentY (direct content scrolling)
    NumberAnimation {
        id: contentScrollAnimation
        target: root.contentItem
        property: "contentY"
        duration: 200
        easing.type: Easing.OutQuad
    }

    // Smooth scrolling animation (fallback)
    NumberAnimation {
        id: scrollAnimation
        target: root
        property: "scrollPosition"
        duration: 200
        easing.type: Easing.OutQuad
        
        onFinished: {
            // Ensure ScrollBar position is synchronized
            if (ScrollBar.vertical) {
                ScrollBar.vertical.position = scrollPosition
            }
        }
    }

    // Also handle when scrollPosition is set externally (e.g., when switching tabs)
    onScrollPositionChanged: {
        if (!scrollAnimation.running && ScrollBar.vertical) {
            ScrollBar.vertical.position = scrollPosition
        }
    }
}
