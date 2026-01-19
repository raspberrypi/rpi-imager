/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RpiImager

/**
 * Base dialog component with common functionality for all dialogs in the application.
 * Provides focus management, consistent styling, and dynamic sizing.
 */
Dialog {
    id: root
    
    // imageWriter property - child dialogs will provide the actual value
    // We declare it here so bindings work, but children override it
    property var imageWriter
    
    // Standard dialog properties
    modal: true
    
    // Reset Dialog's built-in padding - we use our own margins in contentLayout
    padding: 0
    topPadding: 0
    bottomPadding: 0
    leftPadding: 0
    rightPadding: 0
    
    // Dynamic width based on content, with min/max bounds
    // Grows to fit content (especially for long translated strings) but stays within window
    readonly property int minDialogWidth: 400
    readonly property int maxDialogWidth: parent ? Math.max(minDialogWidth, parent.width - Style.cardPadding * 2) : 700
    // Use the largest of: minDialogWidth, explicit implicitWidth, or content-based width
    readonly property int contentBasedWidth: contentLayout ? (contentLayout.implicitWidth + Style.cardPadding * 2) : minDialogWidth
    width: Math.min(maxDialogWidth, Math.max(minDialogWidth, implicitWidth, contentBasedWidth))
    
    // Dynamic height based on content (can be overridden)
    // Use content-based height with a small minimum to ensure dialog is never too tiny
    height: Math.max(100, contentLayout ? (contentLayout.implicitHeight + Style.cardPadding * 2) : 100)
    
    // Positioning - only set if no anchors are used
    x: anchors.centerIn ? 0 : (parent ? (parent.width - width) / 2 : 0)
    y: anchors.centerIn ? 0 : (parent ? (parent.height - height) / 2 : 0)
    
    // Content layout reference for dynamic sizing
    property alias contentLayout: contentLayout
    
    // Allow child dialogs to provide custom content
    default property alias contentData: contentLayout.data
    
    // Custom modal overlay background
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.3)
        Behavior on opacity {
            NumberAnimation {
                duration: 150
            }
        }
    }
    
    // Remove standard dialog buttons since we have custom ones
    standardButtons: Dialog.NoButton
    
    // Set the dialog background directly
    background: Rectangle {
        color: Style.titleBackgroundColor
        radius: (root.imageWriter && root.imageWriter.isEmbeddedMode()) ? Style.sectionBorderRadiusEmbedded : Style.sectionBorderRadius
        border.color: Style.popupBorderColor
        border.width: Style.sectionBorderWidth
        antialiasing: true  // Smooth edges at non-integer scale factors
        clip: true  // Prevent content overflow at non-integer scale factors
    }
    
    // Main content wrapped in FocusScope with focus management system
    FocusScope {
        id: dialogFocusScope
        anchors.fill: parent
        focus: true
        
        // Accessibility properties
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        Accessible.description: ""
        
        Keys.onEscapePressed: {
            // Allow child dialogs to handle escape differently
            root.escapePressed()
        }
        
        // Focus management system
        property var _focusGroups: []
        property var _focusableItems: []
        property var initialFocusItem: null
        
        function registerFocusGroup(name, getItemsFn, order) {
            if (order === undefined) order = 0
            // Replace if exists
            for (var i = 0; i < _focusGroups.length; i++) {
                if (_focusGroups[i].name === name) {
                    _focusGroups[i] = { name: name, getItemsFn: getItemsFn, order: order, enabled: true }
                    rebuildFocusOrder()
                    return
                }
            }
            _focusGroups.push({ name: name, getItemsFn: getItemsFn, order: order, enabled: true })
            rebuildFocusOrder()
        }
        
        function rebuildFocusOrder() {
            // Compose enabled groups by order
            _focusGroups.sort(function(a,b){ return a.order - b.order })
            var items = []
            for (var i = 0; i < _focusGroups.length; i++) {
                var g = _focusGroups[i]
                if (!g.enabled || !g.getItemsFn) continue
                var arr = g.getItemsFn()
                if (!arr || !arr.length) continue
                for (var k = 0; k < arr.length; k++) {
                    var it = arr[k]
                    // Skip items that have activeFocusOnTab explicitly set to false (e.g. text labels when screen reader inactive)
                    if (it && it.visible && it.enabled && typeof it.forceActiveFocus === 'function' && it.activeFocusOnTab !== false) {
                        items.push(it)
                    }
                }
            }
            _focusableItems = items

            // Determine first/last
            var firstField = _focusableItems.length > 0 ? _focusableItems[0] : null
            var lastField = _focusableItems.length > 0 ? _focusableItems[_focusableItems.length-1] : null

            // Wire fields forward/backward (circular navigation)
            for (var j = 0; j < _focusableItems.length; j++) {
                var cur = _focusableItems[j]
                var next = (j + 1 < _focusableItems.length) ? _focusableItems[j+1] : firstField
                var prev = (j > 0) ? _focusableItems[j-1] : lastField
                if (cur && cur.KeyNavigation) {
                    cur.KeyNavigation.tab = next
                    cur.KeyNavigation.backtab = prev
                }
            }

            // Set initial focus item to first focusable item (respecting focus group order)
            var firstField = _focusableItems.length > 0 ? _focusableItems[0] : null
            if (!initialFocusItem) initialFocusItem = firstField
        }
        
        // Main content layout
        ColumnLayout {
            id: contentLayout
            anchors.fill: parent
            anchors.margins: Style.cardPadding
            spacing: Style.spacingMedium
            
            // Make imageWriter available to all children via parent lookup
            property var imageWriter: root.imageWriter
        }
    }
    
    // Expose focus management functions to child dialogs
    function registerFocusGroup(name, getItemsFn, order) {
        dialogFocusScope.registerFocusGroup(name, getItemsFn, order)
    }

    function rebuildFocusOrder() {
        dialogFocusScope.rebuildFocusOrder()
    }
    
    // Default escape handler - child dialogs can override
    function escapePressed() {
        root.close()
    }
    
    onOpened: {
        // Now that dialog is visible, rebuild focus order
        dialogFocusScope.rebuildFocusOrder()
        
        // Ensure the FocusScope gets focus first
        dialogFocusScope.forceActiveFocus()
        // Then focus the initial item
        if (dialogFocusScope.initialFocusItem) {
            dialogFocusScope.initialFocusItem.forceActiveFocus()
        }
    }
}
