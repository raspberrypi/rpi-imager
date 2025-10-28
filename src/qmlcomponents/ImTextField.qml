/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import RpiImager

pragma ComponentBehavior: Bound

TextField {
    id: root

    // Sensible defaults to ensure consistent behavior across the app
    activeFocusOnPress: true
    activeFocusOnTab: true
    selectByMouse: true
    persistentSelection: true
    cursorVisible: activeFocus
    
    // Accessibility properties
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText !== "" ? placeholderText : text
    Accessible.description: ""
    Accessible.editable: true
    Accessible.focused: activeFocus
    Accessible.passwordEdit: echoMode === TextInput.Password

    // Context menu for right-click with cut/copy/paste
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        cursorShape: Qt.IBeamCursor
        propagateComposedEvents: true
        
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                contextMenu.popup()
                mouse.accepted = true
            } else {
                mouse.accepted = false
            }
        }
        
        // Let other events pass through to TextField
        onPressed: function(mouse) {
            if (mouse.button !== Qt.RightButton) {
                mouse.accepted = false
            }
        }
        onReleased: function(mouse) {
            if (mouse.button !== Qt.RightButton) {
                mouse.accepted = false
            }
        }
        onDoubleClicked: function(mouse) {
            mouse.accepted = false
        }
        onPositionChanged: function(mouse) {
            mouse.accepted = false
        }
    }
    
    Menu {
        id: contextMenu
        
        MenuItem {
            text: qsTr("Cut")
            enabled: root.selectedText.length > 0 && !root.readOnly && root.echoMode === TextInput.Normal
            onTriggered: {
                // Copy to clipboard then remove selection
                ClipboardHelper.setText(root.selectedText)
                root.remove(root.selectionStart, root.selectionEnd)
            }
        }
        MenuItem {
            text: qsTr("Copy")
            enabled: root.selectedText.length > 0 && root.echoMode === TextInput.Normal
            onTriggered: {
                ClipboardHelper.setText(root.selectedText)
            }
        }
        MenuItem {
            text: qsTr("Paste")
            enabled: !root.readOnly && ClipboardHelper.hasText
            onTriggered: {
                var clipText = ClipboardHelper.getText()
                if (clipText.length > 0) {
                    root.insert(root.cursorPosition, clipText)
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Select All")
            enabled: root.text.length > 0
            onTriggered: root.selectAll()
        }
    }

    // No special key handling here; rely on WizardStepBase auto wiring
}




