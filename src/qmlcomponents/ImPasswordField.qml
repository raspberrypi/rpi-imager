/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RpiImager

pragma ComponentBehavior: Bound

/**
 * A password input field with an optional "show/hide password" eye icon toggle.
 * 
 * The eye icon is only shown when the user has entered text (text.length > 0),
 * NOT when a password is loaded from saved settings.
 */
Item {
    id: root

    // Public API (forwards from internal TextField)
    property alias text: textField.text
    property alias placeholderText: textField.placeholderText
    property alias font: textField.font
    property alias enabled: textField.enabled
    property alias readOnly: textField.readOnly
    property alias validator: textField.validator
    
    // Forward the internal TextField for focus management and activeFocus access
    // Note: activeFocus is FINAL on Item, so access it via textField.activeFocus
    property alias textField: textField
    
    // Convenience read-only property to check if the text field has focus
    readonly property bool textFieldHasFocus: textField.activeFocus
    
    // Accessibility
    property string accessibleDescription: ""
    
    // Control whether the eye icon should be shown
    // Set to false to hide the eye icon (e.g., when password is loaded from settings and field is empty)
    property bool showToggle: true
    
    // Internal state for password visibility
    property bool passwordVisible: false

    // Layout
    implicitHeight: textField.implicitHeight
    implicitWidth: 200

    // Enable tab navigation to this field
    activeFocusOnTab: true

    // Accessible properties
    Accessible.role: Accessible.EditableText
    Accessible.name: textField.placeholderText !== "" ? textField.placeholderText : textField.text
    Accessible.description: root.accessibleDescription
    Accessible.editable: true
    Accessible.focused: textField.activeFocus
    Accessible.passwordEdit: !passwordVisible

    ImTextField {
        id: textField
        anchors.fill: parent
        
        // Toggle between password and normal mode
        echoMode: passwordVisible ? TextInput.Normal : TextInput.Password
        
        // Make room for the eye icon button
        rightPadding: eyeToggleVisible ? eyeButton.width + 8 : 12
        
        // Accessibility: update the description when toggle state changes
        Accessible.description: {
            var base = root.accessibleDescription
            if (eyeToggleVisible) {
                var toggleHint = passwordVisible 
                    ? qsTr("Password is visible. Press F2 to hide.")
                    : qsTr("Password is hidden. Press F2 to show.")
                return base ? base + " " + toggleHint : toggleHint
            }
            return base
        }
        
        // Handle keyboard shortcut to toggle visibility
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_F2 && eyeToggleVisible) {
                passwordVisible = !passwordVisible
                event.accepted = true
            }
            // Forward Tab/Shift+Tab navigation to parent Item's KeyNavigation
            else if (event.key === Qt.Key_Tab) {
                var nextItem = event.modifiers & Qt.ShiftModifier 
                    ? root.KeyNavigation.backtab 
                    : root.KeyNavigation.tab
                if (nextItem) {
                    nextItem.forceActiveFocus()
                    event.accepted = true
                }
            }
        }
    }
    
    // Computed property: show eye icon only when user has entered text
    readonly property bool eyeToggleVisible: showToggle && textField.text.length > 0
    
    // Eye icon toggle button - using Button for proper accessibility
    Button {
        id: eyeButton
        
        anchors.right: textField.right
        anchors.rightMargin: 4
        anchors.verticalCenter: textField.verticalCenter
        
        // Match the text field height for a balanced look
        height: textField.height - 8
        width: height
        padding: 0
        
        visible: eyeToggleVisible
        opacity: eyeToggleVisible ? 1.0 : 0.0
        
        // Don't include in normal tab order - use F2 shortcut instead
        // This keeps focus flow simple: tab moves between fields, F2 toggles visibility
        focusPolicy: Qt.NoFocus
        
        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }
        
        // Accessibility
        Accessible.role: Accessible.Button
        Accessible.name: passwordVisible ? qsTr("Hide password") : qsTr("Show password")
        Accessible.description: passwordVisible 
            ? qsTr("Password is currently visible. Activate to hide it.")
            : qsTr("Password is currently hidden. Activate to show it.")
        
        background: Rectangle {
            radius: 4
            color: eyeButton.hovered ? Style.buttonHoveredBackgroundColor : "transparent"
            border.width: eyeButton.visualFocus ? Style.focusOutlineWidth : 0
            border.color: Style.focusOutlineColor
        }
        
        contentItem: Image {
            id: eyeIcon
            // Fill most of the button, leaving a small margin
            width: eyeButton.width - 4
            height: eyeButton.height - 4
            source: passwordVisible ? "../icons/ic_eye_hidden_24px.svg" : "../icons/ic_eye_visible_24px.svg"
            fillMode: Image.PreserveAspectFit
            smooth: true
            antialiasing: true
            anchors.centerIn: parent
        }
        
        onClicked: {
            passwordVisible = !passwordVisible
            // Return focus to the text field
            textField.forceActiveFocus()
        }
    }
    
    // Focus handling - delegate to the text field
    onActiveFocusChanged: {
        if (activeFocus) {
            textField.forceActiveFocus()
        }
    }
    
    // Sync KeyNavigation from parent Item to textField
    // This ensures Tab navigation works correctly since focus is on textField but KeyNavigation is set on Item
    function syncKeyNavigation() {
        if (root.KeyNavigation && textField.KeyNavigation) {
            textField.KeyNavigation.tab = root.KeyNavigation.tab
            textField.KeyNavigation.backtab = root.KeyNavigation.backtab
        }
    }
    
    Component.onCompleted: {
        // Sync after a short delay to ensure WizardStepBase has finished setting KeyNavigation
        Qt.callLater(function() {
            syncKeyNavigation()
        })
    }
    
    // Also sync when textField gets focus (in case KeyNavigation was set after Component.onCompleted)
    Connections {
        target: textField
        function onActiveFocusChanged() {
            if (textField.activeFocus) {
                Qt.callLater(syncKeyNavigation)
            }
        }
    }
}

