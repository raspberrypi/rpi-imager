/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtCore

/**
 * Save file dialog - a thin wrapper around ImFileDialog in save mode.
 * 
 * Usage:
 *   ImSaveFileDialog {
 *       dialogTitle: qsTr("Save Performance Data")
 *       suggestedFilename: "data.json"
 *       nameFilters: [qsTr("JSON files (*.json)")]
 *       onAccepted: console.log("Save to:", selectedFile)
 *   }
 */
ImFileDialog {
    id: saveDialog
    
    // Override default title for save dialogs
    dialogTitle: qsTr("Save File")
    
    // Enable save dialog mode
    isSaveDialog: true
    
    // Default to Documents folder for save dialogs
    currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
}
