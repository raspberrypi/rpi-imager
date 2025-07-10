/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "qmlcomponents"

import RpiImager

Flickable {
    id: root
    
    // Reference to the TabBar (set by parent)
    property var tabBar: null
    property var optionsPopup: null
    property int bottomOffset: 0

    width: parent.width
    height: parent.height
    contentWidth: contentItem.children[0].width
    contentHeight: contentItem.children[0].implicitHeight
    clip: true
    
    // Ensure Flickable doesn't interfere with tab navigation
    activeFocusOnTab: false
    focus: false

    ScrollBar.vertical: ScrollBar {
        id: verticalScrollBar
        policy: ScrollBar.AsNeeded
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        
        background: Rectangle {
            color: Style.titleBackgroundColor
        }
        contentItem: Rectangle {
            implicitWidth: 8
            implicitHeight: 100
            color: Style.button2FocusedBackgroundColor
            radius: 4
        }
    }

    function resetScroll() {
        if (verticalScrollBar) {
            root.contentY = 0
        }
    }

    // Auto-scroll functionality
    function ensureVisible(item) {
        if (!item || !item.visible || !item.parent) {
            return
        }

        // Wait a frame to ensure all layout calculations are complete
        Qt.callLater(function() { ensureVisibleImmediate(item) })
    }

    function ensureVisibleImmediate(item) {
        if (!item || !item.visible || !item.parent) return

        // This is the corrected mapping. It gets the item's position relative
        // to the Flickable's content area.
        var itemPos = item.mapToItem(root.contentItem, 0, 0)
        if (!itemPos) return

        var itemY = itemPos.y
        var itemHeight = item.height
        var viewportHeight = height - bottomOffset

        var maxScroll = Math.max(0, contentHeight - viewportHeight)
        var currentScrollY = contentY
        var visibleTop = currentScrollY
        var visibleBottom = currentScrollY + viewportHeight
        var targetScrollY = currentScrollY

        var comfortPadding = 20;
        var comfortTop = visibleTop + comfortPadding;
        var comfortBottom = visibleBottom - comfortPadding;

        if (itemY >= comfortTop && (itemY + itemHeight) <= comfortBottom) {
            return;
        }

        if (itemY < visibleTop) {
            targetScrollY = itemY
        } else if (itemY + itemHeight > visibleBottom) {
            targetScrollY = itemY + itemHeight - viewportHeight
        } else {
            targetScrollY = itemY - (viewportHeight / 2) + (itemHeight / 2)
        }

        if (maxScroll <= 0) {
            return
        }

        var clampedY = Math.max(0, Math.min(targetScrollY, maxScroll))
        
        // Directly set contentY for Flickable
        scrollAnimation.from = contentY
        scrollAnimation.to = clampedY
        scrollAnimation.start()
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

    // Track focus changes using a timer
    Timer {
        id: focusCheckTimer
        interval: 100
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

    onVisibleChanged: {
        if (!visible) {
            focusCheckTimer.lastFocusedItem = null
        }
    }

    // Smooth scrolling animation for Flickable
    NumberAnimation {
        id: scrollAnimation
        target: root
        property: "contentY"
        duration: 200
        easing.type: Easing.OutQuad
    }
}
