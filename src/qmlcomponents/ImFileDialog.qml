/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.folderlistmodel 2.15
import QtCore

Dialog {
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

    modal: true
    standardButtons: Dialog.NoButton
    width: Math.min(720, Math.max(520, parent ? parent.width - 80 : 720))
    height: Math.min(540, Math.max(360, parent ? parent.height - 80 : 540))
    anchors.centerIn: parent

    // Center the dialog on screen
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0

    background: Rectangle {
        color: Style.mainBackgroundColor
        radius: Style.sectionBorderRadius
        border.color: Style.popupBorderColor
        border.width: Style.sectionBorderWidth
    }

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

    Component.onCompleted: {
        // Populate common places (only add if available)
        function addPlace(label, locEnum) {
            try {
                var loc = StandardPaths.writableLocation(locEnum)
                if (loc && String(loc).length > 0) {
                    placesModel.append({ label: label, url: loc })
                }
            } catch (e) {
                // ignore missing locations
            }
        }
        addPlace(qsTr("Home"), StandardPaths.HomeLocation)
        addPlace(qsTr("Documents"), StandardPaths.DocumentsLocation)
        addPlace(qsTr("Downloads"), StandardPaths.DownloadLocation)
        addPlace(qsTr("Pictures"), StandardPaths.PicturesLocation)
        addPlace(qsTr("Music"), StandardPaths.MusicLocation)
        addPlace(qsTr("Movies"), StandardPaths.MoviesLocation)
        // Add root folder explicitly
        placesModel.append({ label: qsTr("Root"), url: "file:///" })
        // Initialize path field text
        pathField.text = dialog._toDisplayPath(dialog.currentFolder)
    }

    onCurrentFolderChanged: {
        // Ensure address bar follows folder changes
        pathField.text = dialog._toDisplayPath(dialog.currentFolder)
        // Reset selection when navigating
        dialog.selectedFile = ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.cardPadding
        spacing: Style.spacingSmall

        // Title
        Text {
            id: titleText
            Layout.fillWidth: true
            text: dialog.dialogTitle
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
        }

        // Address bar (editable)
        TextField {
            id: pathField
            Layout.fillWidth: true
            text: dialog._toDisplayPath(dialog.currentFolder)
            placeholderText: qsTr("Enter path or URL…")
            onAccepted: {
                var newUrl = dialog._toFileUrl(text)
                if (newUrl && newUrl.length > 0) {
                    dialog.currentFolder = newUrl
                }
            }
        }

        // Main content with left navigation and file list
        RowLayout {
            id: mainRow
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.spacingSmall

            // Left navigation pane
            Frame {
                id: leftPane
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                background: Rectangle { color: Style.mainBackgroundColor; radius: Style.sectionBorderRadius; border.color: Style.popupBorderColor; border.width: Style.sectionBorderWidth }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Style.cardPadding
                    spacing: Style.spacingSmall

                    ListView {
                        id: placesList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 160)
                        clip: true
                        model: placesModel
                        delegate: ItemDelegate {
                            width: (ListView.view ? ListView.view.width : 0)
                            text: model.label
                            onClicked: {
                                dialog.currentFolder = model.url
                            }
                        }
                    }

                    Text { text: qsTr("Folders"); font.pixelSize: Style.fontSizeDescription; color: Style.textDescriptionColor; Layout.fillWidth: true }

                    // Up entry in left folders pane
                    ItemDelegate {
                        Layout.fillWidth: true
                        visible: dialog._canGoUp()
                        text: "../"
                        onClicked: dialog._goUp()
                    }

                    ListView {
                        id: subfoldersList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: dirsOnlyModel
                        delegate: ItemDelegate {
                            width: (ListView.view ? ListView.view.width : 0)
                            text: fileName
                            onClicked: {
                                dialog.currentFolder = fileURL
                            }
                        }
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: Style.scrollBarWidth }
                    }
                }
            }

            // File list
            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                background: Rectangle { color: Style.mainBackgroundColor; radius: Style.sectionBorderRadius; border.color: Style.popupBorderColor; border.width: Style.sectionBorderWidth }

                // Unified scroll area: Up entry, then directories, then files
                ScrollView {
                    anchors.fill: parent
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: Style.scrollBarWidth }

                    Column {
                        id: fileColumn
                        width: parent.width
                        spacing: 0

                        // Up entry at top of right pane
                        ItemDelegate {
                            id: upEntry
                            width: parent.width
                            visible: dialog._canGoUp()
                            text: "../"
                            onClicked: dialog._goUp()
                        }

                        // Files
                        Repeater {
                            model: filesOnlyModel
                            delegate: ItemDelegate {
                                width: fileColumn.width
                                text: fileName
                                background: Rectangle {
                                    color: dialog.selectedFile === fileURL ? Style.listViewHighlightColor : "transparent"
                                    radius: Style.listItemBorderRadius
                                }
                                onClicked: {
                                    dialog.selectedFile = fileURL
                                }
                            }
                        }
                    }
                }
            }
        }

        // Buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }
            ImButton {
                text: qsTr("Cancel")
                onClicked: { dialog.close(); dialog.rejected() }
            }
            ImButton {
                text: qsTr("Open")
                enabled: String(dialog.selectedFile).length > 0
                onClicked: { dialog.close(); dialog.accepted() }
            }
        }
    }
}



