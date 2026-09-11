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

    // What the things in this list are called, for the announcements below.
    // Singular and plural because "1 devices available" is worse than not
    // saying it, and several languages need more than an -s.
    property string itemNoun: qsTr("item")
    property string itemNounPlural: qsTr("items")
    // Set false for a list whose population never changes after loading, to
    // keep it out of the accessibility tree entirely.
    property bool announcePopulationChanges: true
    // Whether the first time this list has contents is worth announcing.
    //
    // It depends on where the contents come from, which only the owner
    // knows. A list filled in one go when a fetch lands is *loading*, and
    // announcing that talks over the heading being read as the screen
    // opens. A list backed by something live -- drives appearing and
    // disappearing as they are plugged in -- has no such moment: the first
    // card is the thing the user was waiting for, and staying quiet about
    // it is the whole problem this exists to fix.
    property bool announceFirstPopulation: false
    
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
    focusPolicy: Qt.TabFocus
    boundsBehavior: Flickable.StopAtBounds
    currentIndex: -1

    // Disable kinetic flick scrolling when the OS prefers reduced motion
    maximumFlickVelocity: PlatformHelper.prefersReducedMotion ? 0 : 2500
    flickDeceleration: PlatformHelper.prefersReducedMotion ? 100000 : 1500
    highlightMoveDuration: PlatformHelper.prefersReducedMotion ? 0 : -1
    
    // Keep delegates instantiated beyond the visible area to prevent
    // itemAtIndex() returning null during keyboard/accessibility navigation
    cacheBuffer: 2000
    
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
                    currentIndex = root.findNextSelectableIndex(-1, 1)
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
                    currentIndex = root.findNextSelectableIndex(-1, 1)
                }
            })
        }
        root.announcePopulation()
    }

    // ── Telling a screen reader the list changed ──────────────────────
    //
    // A ListView announces nothing of its own when a row appears or
    // disappears, and a changed Accessible.name on an element nobody is
    // focused on is not read out. Every list in this application is
    // populated by something the user is waiting for -- a network fetch, a
    // card being plugged in -- and a sighted user simply watches it happen.

    // What the alert below is currently saying. Empty when there is nothing
    // to say, which keeps the node out of the tree.
    property string populationAnnouncement: ""
    // -1 until populated once: the first arrival is the list loading, not
    // something that changed while the user was reading it.
    property int lastKnownCount: -1

    function announcePopulation() {
        var now = count
        var before = root.lastKnownCount

        // A ListView emits this once while it is being built, before it has
        // a model worth speaking of. Taking that as the starting population
        // would spend the "first time" allowance on it and then announce the
        // real arrival as though a device had been plugged in.
        if (before < 0 && now === 0)
            return

        root.lastKnownCount = now

        if (!announcePopulationChanges || now === before)
            return

        if (before < 0 && !announceFirstPopulation)
            return

        var noun = (now === 1) ? root.itemNoun : root.itemNounPlural
        if (now > before) {
            root.populationAnnouncement =
                qsTr("A %1 was connected. %2 %3 available.").arg(root.itemNoun).arg(now).arg(noun)
        } else if (now === 0) {
            root.populationAnnouncement =
                qsTr("A %1 was removed. None available.").arg(root.itemNoun)
        } else {
            root.populationAnnouncement =
                qsTr("A %1 was removed. %2 %3 remaining.").arg(root.itemNoun).arg(now).arg(noun)
        }
    }

    // Drawn but transparent, so it is in the accessibility tree without
    // being on screen -- the same shape the storage step already uses for
    // its hidden status text. An alert rather than a status, because it
    // describes something that just happened.
    Label {
        id: populationAlert
        objectName: "populationAnnouncement"
        anchors.fill: parent
        opacity: 0
        text: root.populationAnnouncement
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
        Accessible.ignored: text.length === 0

        // Re-assert the node so the alert is read again when the wording is
        // the same but the event has happened twice -- two cards of the same
        // kind removed in a row, say.
        onTextChanged: {
            if (text.length > 0) {
                Accessible.ignored = true
                Qt.callLater(function() { populationAlert.Accessible.ignored = false })
            }
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
    
    function _activateCurrent(signalFn) {
        if (root.currentIndex === -1 || !root.isItemSelectable(root.currentIndex))
            return
        var item = root.itemAtIndex(root.currentIndex)
        signalFn(root.currentIndex, item)
        root.handleKeyboardSelection(root.currentIndex, item)
    }

    Keys.onSpacePressed: {
        root._activateCurrent(function(i, item) { root.spacePressed(i, item) })
    }

    Keys.onEnterPressed: {
        root._activateCurrent(function(i, item) { root.enterPressed(i, item) })
    }

    Keys.onReturnPressed: {
        root._activateCurrent(function(i, item) { root.returnPressed(i, item) })
    }
    
    // Accessibility support
    Accessible.onPressAction: {
        if (currentIndex !== -1 && root.isItemSelectable(currentIndex)) {
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
    
    // Helper function to select an item programmatically. Refuses an
    // unselectable index for the same reason the key handlers do.
    function selectItem(index) {
        if (index >= 0 && index < count && root.isItemSelectable(index)) {
            currentIndex = index
            var item = itemAtIndex(index)
            root.itemSelected(index, item)
        }
    }
}
