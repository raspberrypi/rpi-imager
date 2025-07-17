/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("Write Image")
    subtitle: qsTr("Ready to write your customized image to the storage device")
    nextButtonText: root.isWriting ? qsTr("Cancel") : (root.isComplete ? qsTr("Continue") : qsTr("Write"))
    nextButtonEnabled: true
    showBackButton: false
    
    property bool isWriting: false
    property bool isComplete: false
    
    // Content
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        spacing: 20
        
        // Summary section
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: summaryLayout.implicitHeight + 40
            Layout.maximumWidth: 500
            Layout.alignment: Qt.AlignHCenter
            color: Style.titleBackgroundColor
            border.color: Style.titleSeparatorColor
            border.width: 1
            radius: 8
            
            ColumnLayout {
                id: summaryLayout
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15
                
                Text {
                    text: qsTr("Summary")
                    font.pixelSize: 16
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
                
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 20
                    rowSpacing: 10
                    
                    Text {
                        text: qsTr("Device:")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                    }
                    
                    Text {
                        text: wizardContainer.selectedDeviceName || qsTr("No device selected")
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        color: Style.formLabelColor
                        Layout.fillWidth: true
                    }
                    
                    Text {
                        text: qsTr("Operating System:")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                    }
                    
                    Text {
                        text: wizardContainer.selectedOsName || qsTr("No image selected")
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        color: Style.formLabelColor
                        Layout.fillWidth: true
                    }
                    
                    Text {
                        text: qsTr("Storage:")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                    }
                    
                    Text {
                        text: wizardContainer.selectedStorageName || qsTr("No storage selected")
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        color: Style.formLabelColor
                        Layout.fillWidth: true
                    }
                }
                
                Text {
                    text: qsTr("Ready to write your customized image to the storage device. All existing data will be erased.")
                    font.pixelSize: 12
                    font.family: Style.fontFamily
                    color: Style.textDescriptionColor
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
        
        // Progress section
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: progressLayout.implicitHeight + 40
            Layout.maximumWidth: 500
            Layout.alignment: Qt.AlignHCenter
            color: Style.titleBackgroundColor
            border.color: Style.titleSeparatorColor
            border.width: 1
            radius: 8
            visible: root.isWriting || root.isComplete
            
            ColumnLayout {
                id: progressLayout
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15
                
                Text {
                    id: progressText
                    text: qsTr("Starting write process...")
                    font.pixelSize: 16
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
                
                ProgressBar {
                    id: progressBar
                    Layout.fillWidth: true
                    Layout.preferredHeight: 20
                    value: 0
                    from: 0
                    to: 100
                    
                    Material.accent: Style.progressBarVerifyForegroundColor
                    Material.background: Style.progressBarBackgroundColor
                    visible: root.isWriting
                }
            }
        }
        
        // Warning section
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: warningLayout.implicitHeight + 40
            Layout.maximumWidth: 500
            Layout.alignment: Qt.AlignHCenter
            color: Style.listViewRowBackgroundColor
            border.color: Style.formLabelErrorColor
            border.width: 2
            radius: 8
            visible: !root.isWriting && !root.isComplete
            
            ColumnLayout {
                id: warningLayout
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15
                
                Text {
                    text: qsTr("⚠️ Warning")
                    font.pixelSize: 16
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelErrorColor
                    Layout.fillWidth: true
                }
                
                Text {
                    text: qsTr("All existing data on '%1' will be erased.\nThis action cannot be undone.").arg(wizardContainer.selectedStorageName || qsTr("the storage device"))
                    font.pixelSize: 12
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
    
    // Handle next button clicks based on current state
    onNextClicked: {
        if (root.isWriting) {
            // Cancel the write
            imageWriter.cancelWrite()
            root.isWriting = false
            wizardContainer.isWriting = false
            progressText.text = qsTr("Write cancelled")
        } else if (!root.isComplete) {
            // Start writing - prevent normal advancement
            root.isWriting = true
            wizardContainer.isWriting = true
            progressText.text = qsTr("Starting write process...")
            progressBar.value = 0
            imageWriter.startWrite()
        } else {
            // Writing is complete, advance to next step
            wizardContainer.nextStep()
        }
    }
    
    function onFinalizing() {
        progressText.text = qsTr("Finalizing...")
        progressBar.value = 100
    }
    
    function onDownloadProgress(now, total) {
        if (root.isWriting) {
            var progress = total > 0 ? (now / total) * 100 : 0
            progressBar.value = progress
            progressText.text = qsTr("Writing... %1%").arg(Math.round(progress))
        }
    }
    
    function onVerifyProgress(now, total) {
        if (root.isWriting) {
            var progress = total > 0 ? (now / total) * 100 : 0
            progressBar.value = progress
            progressText.text = qsTr("Verifying... %1%").arg(Math.round(progress))
        }
    }
    
    function onPreparationStatusUpdate(msg) {
        if (root.isWriting) {
            progressText.text = msg
        }
    }
    
    // Update isWriting state when write completes
    Connections {
        target: imageWriter
        function onSuccess() {
            root.isWriting = false
            wizardContainer.isWriting = false
            root.isComplete = true
            progressText.text = qsTr("Write completed successfully!")
            
            // Automatically advance to the done screen
            wizardContainer.nextStep()
        }
        function onError(msg) {
            root.isWriting = false
            wizardContainer.isWriting = false
            progressText.text = qsTr("Write failed: %1").arg(msg)
        }

        function onFinalizing() {
            if (root.isWriting) {
                root.onFinalizing()
            }
        }
    }
} 