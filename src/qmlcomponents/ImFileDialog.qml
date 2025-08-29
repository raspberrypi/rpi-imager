/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.folderlistmodel 2.15

Dialog {
    id: dialog

    // Public API (aligning loosely with FileDialog)
    property string dialogTitle: qsTr("Select File")
    property url currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    // Back-compat property name some callers may set
    property url folder: currentFolder
    property var nameFilters: [] // ["Images (*.png *.jpg)", "All files (*)"]
    property url selectedFile: ""

    signal accepted()
    signal rejected()

    function open() { dialog.visible = true }
    function close() { dialog.visible = false }

    modal: true
    standardButtons: Dialog.NoButton
    width: Math.min(720, Math.max(520, parent ? parent.width - 80 : 720))
    height: Math.min(540, Math.max(360, parent ? parent.height - 80 : 540))

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

    // Data model
    FolderListModel {
        id: folderModel
        folder: dialog.currentFolder
        showDirs: true
        showFiles: true
        showDotAndDotDot: false
        nameFilters: dialog._extractGlobs(dialog.nameFilters)
        sortField: FolderListModel.Name
        sortReversed: false
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

        // Current folder path
        TextField {
            id: pathField
            Layout.fillWidth: true
            text: String(dialog.currentFolder)
            readOnly: true
        }

        // File list
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { color: Style.mainBackgroundColor; radius: Style.sectionBorderRadius; border.color: Style.popupBorderColor; border.width: Style.sectionBorderWidth }

            ListView {
                id: fileList
                anchors.fill: parent
                clip: true
                model: folderModel
                delegate: ItemDelegate {
                    width: parent.width
                    text: fileName
                    onClicked: {
                        if (fileIsDir) {
                            dialog.currentFolder = fileURL
                        } else {
                            dialog.selectedFile = fileURL
                            fileList.currentIndex = index
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: Style.scrollBarWidth }
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


