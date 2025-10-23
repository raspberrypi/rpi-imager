/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel
import RpiImager

BaseDialog {
    id: dialog

    // Public API (aligning loosely with FileDialog)
    property string dialogTitle: qsTr("Select File")
    property url currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    // Back-compat property name some callers may set
    property url folder: currentFolder
    property var nameFilters: [] // ["Images (*.png *.jpg)", "All files (*)"]
    property url selectedFile: ""

    // Use Dialog's built-in accepted/rejected signals; do not redeclare to avoid overrides

    property bool _firstOpenDone: false
    function open() {
        if (!dialog._firstOpenDone) {
            dialog.currentFolder = "file:///"
            dialog.folder = dialog.currentFolder
            dialog._firstOpenDone = true
        }
        dialog.visible = true
    }
    function close() { dialog.visible = false }

    // Override BaseDialog defaults for file dialog specific sizing
    width: Math.min(720, Math.max(520, parent ? parent.width - 80 : 720))
    height: Math.min(540, Math.max(360, parent ? parent.height - 80 : 540))

    // Convert Qt-style nameFilters to FolderListModel.nameFilters
    function _extractGlobs(filters) {
        var out = []
        for (var i = 0; i < filters.length; i++) {
            var f = String(filters[i])
            var lb = f.indexOf("(")
            var rb = f.indexOf(")")
            if (lb >= 0 && rb > lb) {
                var inner = f.substring(lb+1, rb)
                var globs = inner.split(/\s+/).filter(g => g.length > 0)
                out = out.concat(globs)
            } else if (f.indexOf("*") >= 0) {
                out.push(f)
            }
        }
        if (out.length === 0) out = ["*"]
        return out
    }

    // Normalize a typed path to a file URL
    function _toFileUrl(path) {
        if (!path)
            return "";
        var s = String(path).trim()
        // Expand ~ to home
        if (s === "~" || s.indexOf("~/") === 0) {
            var home = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
            s = home + s.substring(1)
        }
        if (s.indexOf("file://") === 0)
            return s
        // Allow absolute paths
        if (s.indexOf("/") === 0)
            return "file://" + s
        return s // fallback; caller may provide URL
    }

    // Convert a URL to a display path (strip file:// scheme)
    function _toDisplayPath(u) {
        var s = String(u || "").trim()
        if (s.indexOf("file://") === 0) {
            var p = s.substring(7)
            return p.length > 0 ? p : "/"
        }
        return s
    }

    // Get enforced image globs from provided filters; otherwise return defaults
    function _getImageGlobs(filters) {
        var globs = dialog._extractGlobs(filters)
        var specific = globs.filter(function(g) { return g !== "*" })
        if (specific.length > 0)
            return specific
        // Default accepted image formats
        return ["*.img", "*.zip", "*.iso", "*.gz", "*.xz", "*.zst", "*.wic"]
    }

    // Return true if the given url/path resolves to filesystem root
    function _isRoot(u) {
        var s = String(u || "").trim()
        if (s.indexOf("file://") === 0) s = s.substring(7)
        // Normalize trailing slashes
        while (s.length > 1 && s.endsWith("/")) s = s.substring(0, s.length - 1)
        return s === "/"
    }

    // Compute normalized parent folder URL without '..' segments
    function _parentUrl(u) {
        var s = String(u || "").trim()
        if (s.indexOf("file://") === 0) s = s.substring(7)
        // Strip trailing slashes
        while (s.length > 1 && s.endsWith("/")) s = s.substring(0, s.length - 1)
        if (s === "/") return "file:///"
        var slash = s.lastIndexOf("/")
        if (slash <= 0) return "file:///"
        var parent = s.substring(0, slash)
        if (parent.length === 0) parent = "/"
        return "file://" + parent
    }

    function _canGoUp() { return !_isRoot(dialog.currentFolder) }
    function _goUp() { if (_canGoUp()) dialog.currentFolder = _parentUrl(dialog.currentFolder) }

    // Data models
    FolderListModel {
        id: folderModel
        folder: dialog.currentFolder
        showDirs: true
        showFiles: true
        showDotAndDotDot: true
        nameFilters: dialog._extractGlobs(dialog.nameFilters)
        sortField: FolderListModel.Name
        sortReversed: false
    }

    // Directories-only model for left pane (subfolders of current folder)
    FolderListModel {
        id: dirsOnlyModel
        folder: dialog.currentFolder
        showDirs: true
        showFiles: false
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        sortReversed: false
        nameFilters: ["*"]
    }

    // Files-only model for right pane (files of current folder)
    FolderListModel {
        id: filesOnlyModel
        folder: dialog.currentFolder
        showDirs: false
        showFiles: true
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        sortReversed: false
        nameFilters: dialog._getImageGlobs(dialog.nameFilters)
    }

    // Places model for left pane
    ListModel { id: placesModel }

    onCurrentFolderChanged: {
        // Ensure address bar follows folder changes
        pathField.text = dialog._toDisplayPath(dialog.currentFolder)
        // Reset selection when navigating
        dialog.selectedFile = ""
    }

    // Override escape handler to call rejected signal
    function escapePressed() {
        dialog.close()
        dialog.rejected()
    }
    
    // Initialize and register focus groups when component is ready
    Component.onCompleted: {
        // Override contentLayout padding to maximize space
        if (contentLayout) {
            contentLayout.anchors.margins = 10
            contentLayout.spacing = 4
        }
        
        // Add removable drives shortcut based on platform
        var removableDrivesPath = ""
        if (Qt.platform.os === "linux") {
            removableDrivesPath = "file:///mnt"
        } else if (Qt.platform.os === "osx" || Qt.platform.os === "darwin") {
            removableDrivesPath = "file:///Volumes"
        } else if (Qt.platform.os === "windows") {
            // On Windows, show root which lists all drives
            removableDrivesPath = "file:///"
        } else {
            // Fallback to root for other platforms
            removableDrivesPath = "file:///"
        }
        placesModel.append({ label: qsTr("Removable drives"), url: removableDrivesPath })
        
        // Initialize path field text
        pathField.text = dialog._toDisplayPath(dialog.currentFolder)
        
        // Register focus groups
        registerFocusGroup("address", function(){ 
            return [pathField] 
        }, 0)
        registerFocusGroup("places", function(){ 
            return [placesList] 
        }, 1)
        registerFocusGroup("folders", function(){ 
            return [subfoldersList] 
        }, 2)
        registerFocusGroup("navigation", function(){ 
            return [upEntry] 
        }, 3)
        registerFocusGroup("files", function(){ 
            return [filesList] 
        }, 4)
        registerFocusGroup("buttons", function(){ 
            return [cancelButton, openButton] 
        }, 5)
    }

    // Title and address bar on same row
    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 6
        spacing: 12
        
        Text {
            id: titleText
            text: dialog.dialogTitle
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
        }
        
        TextField {
            id: pathField
            Layout.fillWidth: true
            text: dialog._toDisplayPath(dialog.currentFolder)
            placeholderText: qsTr("Enter path or URL…")
            activeFocusOnTab: true
            onAccepted: {
                var newUrl = dialog._toFileUrl(text)
                if (newUrl && newUrl.length > 0) {
                    dialog.currentFolder = newUrl
                }
            }
        }
    }

    // Main content with left navigation and file list
    RowLayout {
        id: mainRow
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 6

        // Left navigation pane
        Frame {
            id: leftPane
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            clip: true
            padding: 8
            background: Rectangle { color: Style.mainBackgroundColor; radius: Style.sectionBorderRadius; border.color: Style.popupBorderColor; border.width: Style.sectionBorderWidth }

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                ListView {
                    id: placesList
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    clip: true
                    activeFocusOnTab: true
                    model: placesModel
                    currentIndex: -1  // No item selected by default
                    highlightFollowsCurrentItem: true
                    highlight: Rectangle {
                        color: placesList.activeFocus ? Style.listViewHighlightColor : Qt.rgba(0, 0, 0, 0.05)
                        radius: Style.listItemBorderRadius
                        visible: placesList.currentIndex >= 0
                    }
                    
                    // Set focus to first item when list receives focus
                    onActiveFocusChanged: {
                        if (activeFocus && currentIndex < 0 && count > 0) {
                            currentIndex = 0
                        }
                    }
                    delegate: ItemDelegate {
                        required property int index
                        required property var model
                        
                        width: (ListView.view ? ListView.view.width : 0)
                        text: model.label
                        highlighted: ListView.isCurrentItem
                        background: Rectangle {
                            color: {
                                if (ListView.isCurrentItem && placesList.activeFocus)
                                    return Style.listViewHighlightColor
                                else if (hovered)
                                    return Style.listViewHoverRowBackgroundColor
                                else
                                    return "transparent"
                            }
                            radius: Style.listItemBorderRadius
                        }
                        onClicked: {
                            placesList.currentIndex = index
                            dialog.currentFolder = model.url
                        }
                    }
                    Keys.onUpPressed: {
                        if (currentIndex > 0) {
                            currentIndex--
                        }
                    }
                    Keys.onDownPressed: {
                        if (currentIndex < count - 1) {
                            currentIndex++
                        }
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0) {
                            dialog.currentFolder = model.get(currentIndex).url
                        }
                    }
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0) {
                            dialog.currentFolder = model.get(currentIndex).url
                        }
                    }
                }

                Text { 
                    text: qsTr("Folders")
                    font.pixelSize: Style.fontSizeDescription
                    color: Style.textDescriptionColor
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    Layout.bottomMargin: 2
                }

                ListView {
                    id: subfoldersList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    activeFocusOnTab: true
                    model: dirsOnlyModel
                    spacing: 2
                    currentIndex: -1  // No item selected by default
                    highlightFollowsCurrentItem: true
                    highlight: Rectangle {
                        color: subfoldersList.activeFocus ? Style.listViewHighlightColor : Qt.rgba(0, 0, 0, 0.05)
                        radius: Style.listItemBorderRadius
                        visible: subfoldersList.currentIndex >= 0
                    }
                    
                    // Set focus to first item when list receives focus
                    onActiveFocusChanged: {
                        if (activeFocus && currentIndex < 0 && count > 0) {
                            currentIndex = 0
                        }
                    }
                    
                    // Reset to first item when folder changes while we have focus
                    onCountChanged: {
                        if (activeFocus && count > 0) {
                            currentIndex = 0
                        }
                    }
                    
                    delegate: ItemDelegate {
                        required property int index
                        required property string fileName
                        required property string fileURL
                        
                        width: (ListView.view ? ListView.view.width : 0)
                        text: "📁 " + fileName
                        highlighted: ListView.isCurrentItem
                        background: Rectangle {
                            color: {
                                if (ListView.isCurrentItem && subfoldersList.activeFocus)
                                    return Style.listViewHighlightColor
                                else if (hovered)
                                    return Style.listViewHoverRowBackgroundColor
                                else
                                    return "transparent"
                            }
                            radius: Style.listItemBorderRadius
                        }
                        onClicked: {
                            subfoldersList.currentIndex = index
                            dialog.currentFolder = fileURL
                        }
                    }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: Style.scrollBarWidth }
                    Keys.onUpPressed: {
                        if (currentIndex > 0) {
                            currentIndex--
                        }
                    }
                    Keys.onDownPressed: {
                        if (currentIndex < count - 1) {
                            currentIndex++
                        }
                    }
                    Keys.onEnterPressed: {
                        if (currentIndex >= 0) {
                            dialog.currentFolder = model.get(currentIndex, "fileURL")
                        }
                    }
                    Keys.onReturnPressed: {
                        if (currentIndex >= 0) {
                            dialog.currentFolder = model.get(currentIndex, "fileURL")
                        }
                    }
                }
            }
        }

        // File list
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 8
            background: Rectangle { color: Style.mainBackgroundColor; radius: Style.sectionBorderRadius; border.color: Style.popupBorderColor; border.width: Style.sectionBorderWidth }

            // Unified scroll area: Up entry, then directories, then files
            ScrollView {
                id: fileListScrollView
                anchors.fill: parent
                activeFocusOnTab: false  // Don't focus the ScrollView itself, only its children
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: Style.scrollBarWidth }
                
                property int currentFileIndex: -1
                
                Keys.onUpPressed: {
                    if (currentFileIndex > 0) {
                        currentFileIndex--
                        // Update selection
                        var fileItem = fileColumn.children[currentFileIndex + 1] // +1 because of up entry
                        if (fileItem && fileItem.fileURL) {
                            dialog.selectedFile = fileItem.fileURL
                        }
                    }
                }
                Keys.onDownPressed: {
                    if (currentFileIndex < filesOnlyModel.count - 1) {
                        currentFileIndex++
                        // Update selection
                        var fileItem = fileColumn.children[currentFileIndex + 1] // +1 because of up entry
                        if (fileItem && fileItem.fileURL) {
                            dialog.selectedFile = fileItem.fileURL
                        }
                    }
                }
                Keys.onEnterPressed: {
                    if (dialog.selectedFile && String(dialog.selectedFile).length > 0) {
                        dialog.close()
                        dialog.accepted()
                    }
                }
                Keys.onReturnPressed: {
                    if (dialog.selectedFile && String(dialog.selectedFile).length > 0) {
                        dialog.close()
                        dialog.accepted()
                    }
                }

                Column {
                    id: fileColumn
                    width: parent.width
                    spacing: 0

                    // Up entry at top of right pane - always show when not at root
                    ImButton {
                        id: upEntry
                        width: parent.width
                        visible: dialog._canGoUp()
                        height: visible ? implicitHeight : 0
                        text: "↑ " + qsTr("Go up a folder")
                        activeFocusOnTab: true
                        onClicked: dialog._goUp()
                        
                        // Rebuild focus order when visibility changes
                        onVisibleChanged: {
                            if (dialog.rebuildFocusOrder) {
                                Qt.callLater(dialog.rebuildFocusOrder)
                            }
                        }
                        
                        // Custom styling to make it look like a navigation item
                        background: Rectangle {
                            color: upEntry.hovered ? Qt.rgba(0, 0, 0, 0.1) : Qt.rgba(0, 0, 0, 0.03)
                            radius: Style.listItemBorderRadius
                            border.width: upEntry.activeFocus ? 2 : 1
                            border.color: upEntry.activeFocus ? Style.focusOutlineColor : Qt.rgba(0, 0, 0, 0.1)
                        }
                        
                        contentItem: Text {
                            text: upEntry.text
                            font.pixelSize: Style.fontSizeDescription
                            font.family: Style.fontFamily
                            font.italic: true
                            color: Style.textDescriptionColor
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: Text.AlignLeft
                            leftPadding: 4
                        }
                    }

                    // Files list with focus support
                    ListView {
                        id: filesList
                        width: fileColumn.width
                        height: Math.max(contentHeight, filesOnlyModel.count === 0 ? 60 : 0)
                        model: filesOnlyModel
                        activeFocusOnTab: filesOnlyModel.count > 0
                        currentIndex: -1
                        highlightFollowsCurrentItem: true
                        interactive: false
                        
                        highlight: Rectangle {
                            color: filesList.activeFocus ? Style.listViewHighlightColor : Qt.rgba(0, 0, 0, 0.05)
                            radius: Style.listItemBorderRadius
                            visible: filesList.currentIndex >= 0
                        }
                        
                        // Set focus to first item when list receives focus
                        onActiveFocusChanged: {
                            if (activeFocus && currentIndex < 0 && count > 0) {
                                currentIndex = 0
                                // Set the selected file to the first file
                                if (count > 0) {
                                    dialog.selectedFile = model.get(0, "fileURL")
                                }
                            }
                        }
                        
                        // Navigate with arrow keys
                        Keys.onUpPressed: {
                            if (currentIndex > 0) {
                                currentIndex--
                                dialog.selectedFile = model.get(currentIndex, "fileURL")
                            }
                        }
                        Keys.onDownPressed: {
                            if (currentIndex < count - 1) {
                                currentIndex++
                                dialog.selectedFile = model.get(currentIndex, "fileURL")
                            }
                        }
                        
                        delegate: ItemDelegate {
                            required property int index
                            required property string fileName
                            required property string fileURL
                            
                            width: fileColumn.width
                            text: "📄 " + fileName
                            highlighted: ListView.isCurrentItem
                            background: Rectangle {
                                color: {
                                    if (dialog.selectedFile === fileURL)
                                        return Style.listViewHighlightColor
                                    else if (ListView.isCurrentItem && filesList.activeFocus)
                                        return Style.listViewHighlightColor
                                    else if (hovered)
                                        return Style.listViewHoverRowBackgroundColor
                                    else
                                        return "transparent"
                                }
                                radius: Style.listItemBorderRadius
                            }
                            onClicked: {
                                filesList.currentIndex = index
                                dialog.selectedFile = fileURL
                            }
                        }
                        
                        Keys.onReturnPressed: {
                            if (dialog.selectedFile && String(dialog.selectedFile).length > 0) {
                                dialog.close()
                                dialog.accepted()
                            }
                        }
                        Keys.onEnterPressed: {
                            if (dialog.selectedFile && String(dialog.selectedFile).length > 0) {
                                dialog.close()
                                dialog.accepted()
                            }
                        }
                        
                        // Show message when no files are available
                        Text {
                            anchors.centerIn: parent
                            visible: filesOnlyModel.count === 0
                            text: qsTr("No files in this folder")
                            font.pixelSize: Style.fontSizeDescription
                            font.family: Style.fontFamily
                            font.italic: true
                            color: Style.textDescriptionColor
                        }
                    }
                }
            }
        }
    }

    // Buttons
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 6
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }
        ImButton {
            id: cancelButton
            text: CommonStrings.cancel
            activeFocusOnTab: true
            onClicked: { dialog.close(); dialog.rejected() }
        }
        ImButton {
            id: openButton
            text: qsTr("Open")
            enabled: String(dialog.selectedFile).length > 0
            activeFocusOnTab: true
            onClicked: { dialog.close(); dialog.accepted() }
        }
    }
}



