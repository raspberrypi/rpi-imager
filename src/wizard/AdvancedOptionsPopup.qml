/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "../qmlcomponents"

import RpiImager

Window {
    id: popup
    width: 400
    height: 280
    
    minimumWidth: width
    minimumHeight: height
    
    title: qsTr("Advanced Options")
    modality: Qt.WindowModal
    
    required property ImageWriter imageWriter
    
    property bool initialized: false
    
    // Shortcut to close with Escape
    Shortcut {
        sequence: "Esc"
        onActivated: popup.close()
    }
    
    // Main content
    Rectangle {
        anchors.fill: parent
        color: Style.titleBackgroundColor
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 20
            
            // Header
            Text {
                text: qsTr("Advanced Options")
                font.pixelSize: 18
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }
            
            // Options section
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Style.titleBackgroundColor
                border.color: Style.titleSeparatorColor
                border.width: 1
                radius: 8
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 15
                    
                    ImCheckBox {
                        id: chkBeep
                        text: qsTr("Play sound when finished")
                        Layout.fillWidth: true
                    }
                    
                    ImCheckBox {
                        id: chkEject
                        text: qsTr("Eject media when finished")
                        Layout.fillWidth: true
                    }
                    
                    ImCheckBox {
                        id: chkTelemetry
                        text: qsTr("Enable telemetry")
                        Layout.fillWidth: true
                    }
                }
            }
            // Spacer
            Item {
                Layout.fillHeight: true
            }
            // Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: 15
                
                Item {
                    Layout.fillWidth: true
                }
                
                ImButton {
                    text: qsTr("Cancel")
                    Layout.minimumWidth: 100
                    onClicked: {
                        popup.close()
                    }
                }
                
                ImButton {
                    text: qsTr("Save")
                    Layout.minimumWidth: 100
                    onClicked: {
                        applySettings()
                        popup.close()
                    }
                }
            }
        }
    }
    
    function initialize() {
        if (!initialized) {
            // Load current settings from ImageWriter
            chkBeep.checked = imageWriter.getBoolSetting("beep")
            chkEject.checked = imageWriter.getBoolSetting("eject")  
            chkTelemetry.checked = imageWriter.getBoolSetting("telemetry")
            initialized = true
        }
    }
    
    function applySettings() {
        // Save settings to ImageWriter
        imageWriter.setSetting("beep", chkBeep.checked)
        imageWriter.setSetting("eject", chkEject.checked)
        imageWriter.setSetting("telemetry", chkTelemetry.checked)
    }
    
    onVisibilityChanged: {
        if (visible) {
            initialize()
            chkBeep.forceActiveFocus()
        }
    }
} 