/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCore
import RpiImager

/**
 * Component for managing multiple SSH public keys.
 * Displays a summary when collapsed, and an expandable list with add/remove functionality.
 */
ColumnLayout {
    id: root
    
    // Public API: list of key strings
    property var keys: []
    
    // Internal state
    property bool expanded: false
    
    spacing: Style.spacingSmall
    
    // Which algorithm a key blob is for.
    //
    // RFC 4716 -- the "---- BEGIN SSH2 PUBLIC KEY ----" format PuTTY exports
    // -- carries no algorithm in its headers, but the blob itself does:
    function sshAlgorithmFromBlob(base64Body) {
        try {
            var raw = Qt.atob(base64Body)
            if (!raw || raw.length < 4)
                return ""
            // Masked to a byte each: Qt.atob returns a string, and Qt warns
            // that its output differs from the Web API's. Every byte read
            // here is in the ASCII range for a well-formed blob -- three
            // zeroes and a small length, then the name -- and the bounds
            // below reject anything that is not, but the mask means a
            // surprising code point cannot become a large length.
            var len = ((raw.charCodeAt(0) & 0xFF) << 24) | ((raw.charCodeAt(1) & 0xFF) << 16)
                    | ((raw.charCodeAt(2) & 0xFF) << 8) | (raw.charCodeAt(3) & 0xFF)
            // A sane algorithm name. The bound rejects a body that decoded to
            // something else entirely rather than trusting its first word.
            if (len <= 0 || len > 64 || raw.length < 4 + len)
                return ""
            var name = raw.substr(4, len)
            return /^[A-Za-z0-9@._-]+$/.test(name) ? name : ""
        } catch (e) {
            return ""
        }
    }

    // Helper function to split text by newlines and filter empty lines
    function splitKeys(text) {
        if (!text || text.length === 0) return []
        var lines = text.split(/\r?\n/)
        var result = []
        var inPutty = false
        var puttyBody = ""
        for (var i = 0; i < lines.length; i++) {
            var trimmed = lines[i].trim()
            // State 1 of PuTTY key (key begins)
            if (trimmed.startsWith("---- BEGIN SSH")) {
                inPutty = true
                puttyBody = ""
                continue
            }
            // Optional(?) state 2 of PuTTY key (comment)
            if (inPutty && trimmed.startsWith("Comment:")) {
                continue
            }
            // Final state (4) of PuTTY key (end of key)
            if (trimmed.startsWith("---- END SSH")) {
                // FIXME: put comment text after the key?
                if (inPutty && puttyBody.length > 0) {
                    // ssh-rsa only as a fallback for a blob we could not
                    // read; naming the algorithm after the blob is what
                    // keeps a non-RSA PuTTY key working.
                    var algorithm = sshAlgorithmFromBlob(puttyBody) || "ssh-rsa"
                    result.push(algorithm + " " + puttyBody)
                }
                inPutty = false
                puttyBody = ""
                continue
            }
            // We'll be in state 3 of PuTTY key (the key itself) for a few lines.
            if (inPutty) {
                puttyBody += trimmed
                continue
            }
            if (trimmed.length > 0) {
                result.push(trimmed)
            }
        }
        return result
    }
    
    // Helper function to deduplicate keys
    function deduplicateKeys(keyList) {
        // Object.create(null) rather than {}: a plain object inherits
        // Object.prototype, so `seen["constructor"]` is truthy before
        // anything has been seen and the line is dropped as a duplicate of
        // nothing. Every key in the file is attacker-chosen text as far as
        // this loop is concerned.
        var seen = Object.create(null)
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

    // What this component contributes to the wizard step's focus ring, in
    // reading order.
    //
    // WizardStepBase builds the ring from the groups a step registers, and it
    // does not walk into nested components -- so a control that is not
    // returned here cannot be reached from the keyboard at all. That is what
    // had happened to the Show button: with keys already configured the step
    // opens with this whole component on screen and none of it in the ring,
    // so keyboard and screen-reader users could see the keys and reach
    // nothing to do with them.
    function focusItems() {
        var items = [summaryText, expandButton]
        if (!root.expanded)
            return items
        for (var i = 0; i < keysRepeater.count; i++) {
            var row = keysRepeater.itemAt(i)
            if (row && row.removeButton)
                items.push(row.removeButton)
        }
        items.push(addKeyField)
        items.push(addOrBrowseButton)
        return items
    }

    signal focusItemsUpdated()

    onExpandedChanged: root.focusItemsUpdated()
    onKeysChanged: root.focusItemsUpdated()
    
    // Summary row (always visible)
    RowLayout {
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        
        FocusableText {
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
            font.pointSize: Style.fontSizeFormLabel
            color: Style.formLabelColor
            Layout.fillWidth: true
            Accessible.ignored: false
        }
        
        ImButton {
            id: expandButton
            objectName: "sshShowKeysButton"
            text: root.expanded ? qsTr("Hide") : qsTr("Show")
            Layout.minimumWidth: 80
            onClicked: root.expanded = !root.expanded
            accessibleDescription: root.expanded 
                ? qsTr("Hide the list of SSH keys")
                : qsTr("Show the list of SSH keys")
        }
    }
    
    // Expanded content (keys list and add button)
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.expanded
        spacing: Style.spacingSmall
        Accessible.role: Accessible.Grouping
        Accessible.name: qsTr("SSH keys list")
        
        // List of keys
        Repeater {
            id: keysRepeater
            model: root.keys
            
            RowLayout {
                id: keyRow
                Layout.fillWidth: true
                spacing: Style.spacingSmall

                // Model data injected by the Repeater. These must be declared as
                // required properties under `pragma ComponentBehavior: Bound`;
                // otherwise the implicitly-injected context properties resolve to
                // undefined in compiled bindings, leaving the key text blank and
                // the Remove button inert. See issue #1664.
                required property int index
                required property var modelData

                // Accessibility for the row as a list item
                Accessible.role: Accessible.ListItem
                Accessible.name: {
                    // Provide full key info for screen readers
                    var keyText = keyRow.modelData
                    var parts = keyText.split(/\s+/)
                    if (parts.length >= 2) {
                        var keyType = parts[0]
                        var comment = parts.length > 2 ? parts.slice(2).join(" ") : ""
                        if (comment) {
                            return qsTr("SSH key %1: %2, %3").arg(keyRow.index + 1).arg(keyType).arg(comment)
                        } else {
                            return qsTr("SSH key %1: %2").arg(keyRow.index + 1).arg(keyType)
                        }
                    }
                    return qsTr("SSH key %1").arg(keyRow.index + 1)
                }
                Accessible.ignored: false
                Accessible.focusable: ImageWriterSingleton ? ImageWriterSingleton.screenReaderActive : false
                focusPolicy: (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) ? Qt.TabFocus : Qt.NoFocus
                activeFocusOnTab: ImageWriterSingleton ? ImageWriterSingleton.screenReaderActive : false
                
                // Key text (truncated for display)
                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: parent ? parent.width - 100 : 500
                    text: {
                        var keyText = keyRow.modelData
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
                    font.pointSize: Style.fontSizeInput
                    font.family: "monospace"
                    color: Style.formLabelColor
                    elide: Text.ElideRight
                    Accessible.ignored: true  // Parent row provides accessibility
                }
                
                // Named and aliased so focusItems() below can hand it to the
                // step: a Repeater's delegates are only reachable through
                // itemAt(), and the focus ring has to be told about each one.
                property alias removeButton: removeKeyButton

                // Remove button
                ImButton {
                    id: removeKeyButton
                    objectName: "sshRemoveKeyButton"
                    text: qsTr("Remove")
                    Layout.minimumWidth: 80
                    onClicked: root.removeKey(keyRow.index)
                    accessibleDescription: {
                        var keyText = keyRow.modelData
                        var parts = keyText.split(/\s+/)
                        if (parts.length > 2) {
                            return qsTr("Remove SSH key: %1").arg(parts.slice(2).join(" "))
                        }
                        return qsTr("Remove SSH key %1").arg(keyRow.index + 1)
                    }
                }
            }
        }
        
        // Add key section
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            
            ImTextField {
                id: addKeyField
                objectName: "sshAddKeyField"
                Layout.fillWidth: true
                placeholderText: qsTr("Paste key or click BROWSE to select file")
                font.pointSize: Style.fontSizeInput
                Accessible.name: qsTr("SSH public key input")
                Accessible.description: qsTr("Paste an SSH public key here or use the browse button to select a key file")
                trimWhitespace: true
                onAccepted: {
                    if (addKeyField.value.length > 0) {
                        root.addKey(addKeyField.value)
                        text = ""
                    }
                }
            }
            
            ImButton {
                id: addOrBrowseButton
                objectName: "sshAddOrBrowseButton"
                text: addKeyField.value.length > 0 ? qsTr("Add") : CommonStrings.browse
                Layout.minimumWidth: 80
                onClicked: {
                    if (addKeyField.value.length > 0) {
                        root.addKey(addKeyField.value)
                        addKeyField.text = ""
                    } else {
                        // Browse for file
                        if (ImageWriterSingleton.nativeFileDialogAvailable()) {
                            var home = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
                            var startDir = home && home.length > 0 ? home + "/.ssh" : ""
                            var picked = ImageWriterSingleton.getNativeOpenFileName(qsTr("Select SSH Public Key"), startDir, CommonStrings.sshFiltersString)
                            if (picked && picked.length > 0) {
                                var contents = ImageWriterSingleton.readFileContents(picked)
                                if (contents && contents.length > 0) {
                                    root.addKeysFromFile(contents)
                                }
                            }
                        } else {
                            browseKeyFileDialog.open()
                        }
                    }
                }
                accessibleDescription: addKeyField.value.length > 0 
                    ? qsTr("Add the entered SSH key")
                    : qsTr("Select an SSH public key file to add")
            }
        }
    }
    
    // File dialog for browsing keys
    ImFileDialog {
        id: browseKeyFileDialog
        objectName: "sshBrowseKeyFileDialog"
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
                var contents = ImageWriterSingleton.readFileContents(filePath)
                if (contents && contents.length > 0) {
                    root.addKeysFromFile(contents)
                }
            }
        }
    }
}
