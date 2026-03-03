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
    
    // Filter-as-you-type state
    property string searchString: ""
    property int originalIndex: -1
    property var fullModelData: []
    
    // Scroll settings for native-like wheel behavior
    readonly property int itemHeight: 40
    readonly property int wheelScrollItems: 3
    
    // Configuration
    selectTextByMouse: true
    editable: false
    
    ListModel {
        id: filteredModel
    }
    
    function wordBoundaryMatch(text, search) {
        var textLower = text.toLowerCase()
        var pos = 0
        while (pos <= textLower.length - search.length) {
            var idx = textLower.indexOf(search, pos)
            if (idx === -1) return false
            if (idx === 0 || /[\s,()/]/.test(textLower.charAt(idx - 1)))
                return true
            pos = idx + 1
        }
        return false
    }
    
    function rebuildFilteredModel() {
        filteredModel.clear()
        var search = searchString.toLowerCase()
        for (var i = 0; i < fullModelData.length; i++) {
            var text = fullModelData[i]
            if (search.length === 0 ||
                text.toLowerCase().startsWith(search) ||
                wordBoundaryMatch(text, search)) {
                filteredModel.append({displayText: text, originalIndex: i})
            }
        }
        if (filteredModel.count > 0) {
            dropdownList.currentIndex = 0
        }
    }
    
    function performSearch(inputText) {
        searchString += inputText.toLowerCase()
        rebuildFilteredModel()
    }
    
    function handleBackspace() {
        if (searchString.length > 0) {
            searchString = searchString.substring(0, searchString.length - 1)
            rebuildFilteredModel()
        }
    }
    
    function selectFilteredItem(filteredIdx) {
        if (filteredIdx >= 0 && filteredIdx < filteredModel.count) {
            var originalIdx = filteredModel.get(filteredIdx).originalIndex
            root.currentIndex = originalIdx
            root.activated(originalIdx)
        }
        popupComponent.close()
    }
    
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

    // Provide a default popup with filter-as-you-type search
    popup: Popup {
        id: popupComponent
        padding: 0
        y: root.height
        width: root.width
        // Cap height to available space between the combo box bottom and window bottom
        height: {
            var ideal = Math.min(300, Math.max(150, root.model.length * root.itemHeight))
            var win = root.Window.window
            if (win) {
                var globalY = root.mapToItem(null, 0, root.height).y
                var margin = 8
                var available = win.height - globalY - margin
                return Math.max(root.itemHeight * 2, Math.min(ideal, available))
            }
            return ideal
        }
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        
        onVisibleChanged: {
            if (visible) {
                root.originalIndex = root.currentIndex
                root.searchString = ""
                // Snapshot the full model into a JS array for filtering
                root.fullModelData = []
                for (var i = 0; i < root.model.length; i++) {
                    root.fullModelData.push(root.textAt ? root.textAt(i) : (root.model[i] + ""))
                }
                root.rebuildFilteredModel()
                // Scroll to the currently selected item
                for (var j = 0; j < filteredModel.count; j++) {
                    if (filteredModel.get(j).originalIndex === root.currentIndex) {
                        dropdownList.currentIndex = j
                        break
                    }
                }
                dropdownList.forceActiveFocus()
            } else {
                root.searchString = ""
                filteredModel.clear()
            }
        }
        
        background: Rectangle {
            color: Style.mainBackgroundColor
            radius: (root.imageWriter && root.imageWriter.isEmbeddedMode()) ? Style.sectionBorderRadiusEmbedded : Style.sectionBorderRadius
            border.color: Style.popupBorderColor
            border.width: Style.sectionBorderWidth
            antialiasing: true
            clip: true
        }
        
        contentItem: Item {
            // Search indicator bar at the top of the popup
            Rectangle {
                id: searchIndicator
                visible: root.searchString.length > 0
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: visible ? 28 : 0
                color: Style.buttonFocusedBackgroundColor
                z: 1
                
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Style.spacingTiny
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Search: \"%1\"").arg(root.searchString)
                    font.pixelSize: Style.fontSizeSmall
                    font.italic: true
                    color: Style.textDescriptionColor
                }
                
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: Style.spacingTiny
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 of %2").arg(filteredModel.count).arg(root.fullModelData.length)
                    font.pixelSize: Style.fontSizeSmall - 2
                    font.italic: true
                    color: Style.textMetadataColor
                }
            }
            
            ListView {
                id: dropdownList
                clip: true
                anchors.top: searchIndicator.visible ? searchIndicator.bottom : parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                model: filteredModel
                boundsBehavior: Flickable.StopAtBounds
            
            focus: popupComponent.visible
            interactive: true
            flickDeceleration: 3000
            maximumFlickVelocity: 2500
            
            preferredHighlightBegin: 0
            preferredHighlightEnd: height
            highlightRangeMode: ListView.ApplyRange
            
            // Placeholder when filter yields no results
            Text {
                anchors.centerIn: parent
                visible: filteredModel.count === 0 && root.searchString.length > 0
                text: qsTr("No matches")
                font.pixelSize: Style.fontSizeSmall
                font.italic: true
                color: Style.textMetadataColor
            }
            
            delegate: ItemDelegate {
                id: filterDelegate
                required property string displayText
                required property int originalIndex
                required property int index
                
                width: parent ? parent.width : 0
                height: root.itemHeight
                padding: 0
                leftPadding: Style.spacingTiny
                rightPadding: Style.spacingTiny
                topPadding: 0
                bottomPadding: 0

                contentItem: Text {
                    text: filterDelegate.displayText
                    font: root.font
                    verticalAlignment: Text.AlignVCenter
                    color: root.indicateError ? Style.formLabelErrorColor : Style.textDescriptionColor
                }
                
                highlighted: dropdownList.currentIndex === filterDelegate.index
                
                onClicked: {
                    root.selectFilteredItem(filterDelegate.index)
                }
                
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Escape) {
                        root.currentIndex = root.originalIndex
                        popupComponent.close()
                        event.accepted = true
                    }
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        root.selectFilteredItem(dropdownList.currentIndex)
                        event.accepted = true
                    }
                    else if (event.key === Qt.Key_Backspace) {
                        root.handleBackspace()
                        event.accepted = true
                    }
                    else if (event.key === Qt.Key_Space && root.searchString.length > 0) {
                        root.performSearch(" ")
                        event.accepted = true
                    }
                    else if (event.key === Qt.Key_Tab) {
                        event.accepted = false
                    }
                    else if (event.text && event.text.length === 1) {
                        root.performSearch(event.text)
                        event.accepted = true
                    }
                }
            }
            
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    root.currentIndex = root.originalIndex
                    popupComponent.close()
                    event.accepted = true
                }
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.selectFilteredItem(dropdownList.currentIndex)
                    event.accepted = true
                }
                else if (event.key === Qt.Key_Backspace) {
                    root.handleBackspace()
                    event.accepted = true
                }
                else if (event.key === Qt.Key_Space && root.searchString.length > 0) {
                    root.performSearch(" ")
                    event.accepted = true
                }
                else if (event.key === Qt.Key_Tab) {
                    event.accepted = false
                }
                else if (event.text && event.text.length === 1) {
                    root.performSearch(event.text)
                    event.accepted = true
                }
            }
            
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
                antialiasing: true
                
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
                    listScrollAnimation.stop()
                    
                    var dy
                    if (event.pixelDelta && Math.abs(event.pixelDelta.y) > 0) {
                        dy = event.pixelDelta.y
                    } else {
                        var notches = event.angleDelta.y / 120.0
                        dy = notches * root.itemHeight * root.wheelScrollItems
                    }
                    
                    // Handle OS scroll direction preference (natural vs traditional)
                    if (PlatformHelper.isScrollInverted(event.inverted)) {
                        dy = -dy
                    }
                    
                    var newY = dropdownList.contentY + dy
                    var maxY = Math.max(0, dropdownList.contentHeight - dropdownList.height)
                    newY = Math.max(0, Math.min(newY, maxY))
                    
                    listScrollAnimation.to = newY
                    listScrollAnimation.restart()
                    
                    event.accepted = true
                }
            }
            }
        } // Item contentItem
    }
}
