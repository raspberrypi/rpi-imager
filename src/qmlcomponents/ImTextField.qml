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

    font.family: Style.fontFamily
    font.pointSize: Style.fontSizeInput

    // Whether surrounding whitespace is meaningful for this field. Fields where it
    // never is (URLs, hostnames, usernames, SSIDs, tokens) set this to true and
    // consumers read `value`. Left false for passwords, where a leading or
    // trailing space is a legitimate part of the secret and silently discarding
    // it would change what the user actually set.
    property bool trimWhitespace: false

    // The sanitised field contents. Read this rather than `text`: it is always
    // free of control characters, and of surrounding whitespace when the field
    // opts in above. Trimming happens here rather than in the scrubber below so
    // that typing a space mid-phrase is not swallowed as you type.
    readonly property string value: trimWhitespace ? text.trim() : text

    // A single-line field must never hold control characters, but Qt inserts
    // pasted text verbatim (QQuickTextInputPrivate::paste() is just
    // insert(clip), and internalInsert() does no filtering). Text copied from a
    // browser therefore arrives with a trailing newline, which has silently
    // corrupted a password hash (issue #1627) and a repository URL (issue
    // #1687). A validator cannot be used for this: QRegularExpressionValidator
    // returns Intermediate rather than Invalid, so Qt keeps the offending text
    // and merely clears acceptableInput. Nor can paste() be overridden here,
    // because the Ctrl/Cmd+V key handler calls the private d->paste() directly.
    // Scrubbing on change is what catches every route in, so that no consumer
    // has to remember to sanitise. These characters cannot be typed, so removing
    // them never disturbs editing, and doing so is lossless even for passwords.
    property bool _scrubbing: false
    onTextChanged: {
        if (_scrubbing)
            return
        var cleaned = text.replace(/[\x00-\x1F\x7F]/g, "")
        if (cleaned === text)
            return
        _scrubbing = true
        var restoreCursor = Math.min(cursorPosition, cleaned.length)
        text = cleaned
        cursorPosition = restoreCursor
        _scrubbing = false
    }

    // Sensible defaults to ensure consistent behavior across the app
    activeFocusOnPress: true
    activeFocusOnTab: true
    focusPolicy: Qt.TabFocus
    selectByMouse: true
    persistentSelection: true
    cursorVisible: activeFocus
    
    // Accessibility properties.
    // Never expose text content via Accessible.name: for password fields this would
    // leak the secret to assistive tech, and for normal fields VoiceOver already
    // announces the value separately via the EditableText role.
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText
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

        // On Linux (X11/Wayland), QClipboard::dataChanged is not reliably
        // emitted for external clipboard changes, so force a fresh check
        // each time the context menu opens.
        onAboutToShow: ClipboardHelper.refresh()

        MenuItem {
            text: qsTr("Cut")
            enabled: root.selectedText.length > 0 && !root.readOnly && root.echoMode === TextInput.Normal
            Accessible.role: Accessible.MenuItem
            Accessible.name: text
            onTriggered: {
                // Copy to clipboard then remove selection
                ClipboardHelper.setText(root.selectedText)
                root.remove(root.selectionStart, root.selectionEnd)
            }
        }
        MenuItem {
            text: qsTr("Copy")
            enabled: root.selectedText.length > 0 && root.echoMode === TextInput.Normal
            Accessible.role: Accessible.MenuItem
            Accessible.name: text
            onTriggered: {
                ClipboardHelper.setText(root.selectedText)
            }
        }
        MenuItem {
            text: qsTr("Paste")
            enabled: !root.readOnly && ClipboardHelper.hasText
            Accessible.role: Accessible.MenuItem
            Accessible.name: text
            onTriggered: root.paste()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Select All")
            enabled: root.text.length > 0
            Accessible.role: Accessible.MenuItem
            Accessible.name: text
            onTriggered: root.selectAll()
        }
    }

    // No special key handling here; rely on WizardStepBase auto wiring
}




