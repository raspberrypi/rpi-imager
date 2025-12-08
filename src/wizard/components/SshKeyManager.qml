/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import RpiImager

/**
 * Component for managing multiple SSH public keys.
 * Displays a summary when collapsed, and an expandable list with add/remove functionality.
 */
ColumnLayout {
    id: root
    
    required property ImageWriter imageWriter
    
    // Public API: list of key strings
    property var keys: []
    
    // Internal state
    property bool expanded: false
    
    spacing: Style.spacingSmall
    
    // Helper function to split text by newlines and filter empty lines
    function splitKeys(text) {
        if (!text || text.length === 0) return []
        var lines = text.split(/\r?\n/)
        var result = []
        for (var i = 0; i < lines.length; i++) {
            var trimmed = lines[i].trim()
            if (trimmed.length > 0) {
                result.push(trimmed)
            }
        }
        return result
    }
    
    // Helper function to deduplicate keys
    function deduplicateKeys(keyList) {
        var seen = {}
        var result = []
        for (var i = 0; i < keyList.length; i++) {
            var key = keyList[i]
            if (!seen[key]) {
                seen[key] = true
                result.push(key)
            }
        }
        return result
    }
    
    // Add keys from file content (split by newlines)
    function addKeysFromFile(fileContent) {
        var newKeys = splitKeys(fileContent)
        var combined = root.keys.concat(newKeys)
        root.keys = deduplicateKeys(combined)
    }
    
    // Add a single key
    function addKey(key) {
        var trimmed = key.trim()
        if (trimmed.length > 0) {
            // Check if key already exists
            for (var i = 0; i < root.keys.length; i++) {
                if (root.keys[i] === trimmed) {
                    return  // Already exists
                }
            }
            root.keys = root.keys.concat([trimmed])
        }
    }
    
    // Remove a key by index
    function removeKey(index) {
        if (index >= 0 && index < root.keys.length) {
            var newKeys = []
            for (var i = 0; i < root.keys.length; i++) {
                if (i !== index) {
                    newKeys.push(root.keys[i])
                }
            }
            root.keys = newKeys
        }
    }
    
    // Get all keys as a single string (newline-separated)
    function getAllKeysAsString() {
        return root.keys.join("\n")
    }
    
    // Summary row (always visible)
    RowLayout {
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        
        Text {
            id: summaryText
            text: {
                if (root.keys.length === 0) {
                    return qsTr("No SSH keys configured")
                } else if (root.keys.length === 1) {
                    return qsTr("1 SSH key configured")
                } else {
                    return qsTr("%1 SSH keys configured").arg(root.keys.length)
                }
            }
            font.pixelSize: Style.fontSizeFormLabel
            color: Style.formLabelColor
            Layout.fillWidth: true
        }
        
        ImButton {
            id: expandButton
            text: root.expanded ? qsTr("Hide") : qsTr("Show")
            Layout.minimumWidth: 80
            onClicked: root.expanded = !root.expanded
            Accessible.description: root.expanded 
                ? qsTr("Hide the list of SSH keys")
                : qsTr("Show the list of SSH keys")
        }
    }
    
    // Expanded content (keys list and add button)
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.expanded
        spacing: Style.spacingSmall
        
        // List of keys
        Repeater {
            id: keysRepeater
            model: root.keys
            
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingSmall
                
                // Key text (truncated for display)
                Text {
                    Layout.fillWidth: true
                    text: {
                        var keyText = modelData
                        // Parse SSH key format: keytype keydata [comment]
                        var parts = keyText.split(/\s+/)
                        if (parts.length >= 2) {
                            var keyType = parts[0]
                            var keyData = parts[1]
                            var comment = parts.length > 2 ? parts.slice(2).join(" ") : ""
                            
                            // Build display: "keytype start...end comment"
                            // Keep key data portion small to leave room for comment
                            var result = keyType + " "
                            
                            if (keyData.length > 20) {
                                result += keyData.substring(0, 8) + "..." + keyData.substring(keyData.length - 8)
                            } else {
                                result += keyData
                            }
                            
                            if (comment) {
                                result += " " + comment
                            }
                            
                            return result
                        }
                        // Fallback for malformed keys
                        if (keyText.length > 40) {
                            return keyText.substring(0, 15) + "..." + keyText.substring(keyText.length - 15)
                        }
                        return keyText
                    }
                    font.pixelSize: Style.fontSizeInput
                    font.family: "monospace"
                    color: Style.formLabelColor
                    elide: Text.ElideRight
                }
                
                // Remove button
                ImButton {
                    text: qsTr("Remove")
                    Layout.minimumWidth: 80
                    onClicked: root.removeKey(index)
                    Accessible.description: qsTr("Remove this SSH key")
                }
            }
        }
        
        // Add key section
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            
            ImTextField {
                id: addKeyField
                Layout.fillWidth: true
                placeholderText: qsTr("Paste key or click BROWSE to select file")
                font.pixelSize: Style.fontSizeInput
                onAccepted: {
                    if (text.trim().length > 0) {
                        root.addKey(text)
                        text = ""
                    }
                }
            }
            
            ImButton {
                id: addOrBrowseButton
                text: addKeyField.text.trim().length > 0 ? qsTr("Add") : CommonStrings.browse
                Layout.minimumWidth: 80
                onClicked: {
                    if (addKeyField.text.trim().length > 0) {
                        root.addKey(addKeyField.text)
                        addKeyField.text = ""
                    } else {
                        // Browse for file
                        if (root.imageWriter.nativeFileDialogAvailable()) {
                            var home = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
                            var startDir = home && home.length > 0 ? home + "/.ssh" : ""
                            var picked = root.imageWriter.getNativeOpenFileName(qsTr("Select SSH Public Key"), startDir, CommonStrings.sshFiltersString)
                            if (picked && picked.length > 0) {
                                var contents = root.imageWriter.readFileContents(picked)
                                if (contents && contents.length > 0) {
                                    root.addKeysFromFile(contents)
                                }
                            }
                        } else {
                            browseKeyFileDialog.open()
                        }
                    }
                }
                Accessible.description: addKeyField.text.trim().length > 0 
                    ? qsTr("Add the entered SSH key")
                    : qsTr("Select an SSH public key file to add")
            }
        }
    }
    
    // File dialog for browsing keys
    ImFileDialog {
        id: browseKeyFileDialog
        imageWriter: root.imageWriter
        parent: root.parent
        anchors.centerIn: parent
        dialogTitle: qsTr("Select SSH Public Key")
        nameFilters: CommonStrings.sshFiltersList
        Component.onCompleted: {
            var home = StandardPaths.writableLocation(StandardPaths.HomeLocation)
            var url = "file://" + home + "/.ssh"
            browseKeyFileDialog.currentFolder = url
            browseKeyFileDialog.folder = url
        }
        onAccepted: {
            if (selectedFile && selectedFile.toString().length > 0) {
                var filePath = selectedFile.toString().replace(/^file:\/\//, "")
                var contents = root.imageWriter.readFileContents(filePath)
                if (contents && contents.length > 0) {
                    root.addKeysFromFile(contents)
                }
            }
        }
    }
}
