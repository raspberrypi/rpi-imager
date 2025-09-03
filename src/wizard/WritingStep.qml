/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("Write Image")
    subtitle: qsTr("Review your choices and write the image to the storage device")
    nextButtonText: root.isWriting && root.isVerifying ? qsTr("Skip verification") : (root.isWriting ? qsTr("Cancel") : (root.isComplete ? qsTr("Continue") : qsTr("Write")))
    nextButtonEnabled: true
    showBackButton: true
    
    property bool isWriting: false
    property bool isComplete: false
    property bool isVerifying: false
    property bool confirmOpen: false
    readonly property bool anyCustomizationsApplied: (
        wizardContainer.hostnameConfigured ||
        wizardContainer.localeConfigured ||
        wizardContainer.userConfigured ||
        wizardContainer.wifiConfigured ||
        wizardContainer.sshEnabled ||
        wizardContainer.piConnectEnabled
    )
    
    // Disable back while writing
    backButtonEnabled: !root.isWriting

    // Content
    content: [
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.cardPadding
        spacing: Style.spacingLarge

        // Top spacer to vertically center progress section when writing/complete
        Item { Layout.fillHeight: true; visible: root.isWriting || root.isComplete }
        
        // Summary section (de-chromed)
        ColumnLayout {
            id: summaryLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Style.sectionMaxWidth
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.spacingMedium
            visible: !root.isWriting

            Text {
                text: qsTr("Summary")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
            }
            
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.spacingSmall
                
                Text {
                    text: qsTr("Device:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                }
                
                Text {
                    text: wizardContainer.selectedDeviceName || qsTr("No device selected")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
                
                Text {
                    text: qsTr("Operating System:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                }
                
                Text {
                    text: wizardContainer.selectedOsName || qsTr("No image selected")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
                
                Text {
                    text: qsTr("Storage:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                }
                
                Text {
                    text: wizardContainer.selectedStorageName || qsTr("No storage selected")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
            }
            
            Text {
                text: root.anyCustomizationsApplied 
                      ? qsTr("Ready to write your customized image to the storage device. All existing data will be erased.")
                      : qsTr("Ready to write the image to the storage device. All existing data will be erased.")
                font.pixelSize: Style.fontSizeDescription
                font.family: Style.fontFamily
                color: Style.textDescriptionColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        
        // Customization summary (what will be written) - de-chromed
        ColumnLayout {
            id: customLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Style.sectionMaxWidth
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.spacingMedium
            visible: !root.isWriting && root.anyCustomizationsApplied

            Text {
                text: qsTr("Customizations to apply:")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
            }

            Column {
                Layout.fillWidth: true
                spacing: Style.spacingXSmall

                Text { text: qsTr("• Hostname configured");        font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.hostnameConfigured }
                Text { text: qsTr("• User account configured");    font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.userConfigured }
                Text { text: qsTr("• WiFi configured");            font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.wifiConfigured }
                Text { text: qsTr("• SSH enabled");                font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.sshEnabled }
                Text { text: qsTr("• Pi Connect enabled");        font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.piConnectEnabled }
                Text { text: qsTr("• Locale configured");         font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.localeConfigured }
            }
        }

        // Progress section (de-chromed)
        ColumnLayout {
            id: progressLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Style.sectionMaxWidth
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.spacingMedium
            visible: root.isWriting || root.isComplete
            
            Text {
                id: progressText
                text: qsTr("Starting write process...")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }
            
            ProgressBar {
                id: progressBar
                Layout.fillWidth: true
                Layout.preferredHeight: Style.spacingLarge
                value: 0
                from: 0
                to: 100
                
                // Default to write color; updated dynamically in onDownloadProgress/onVerifyProgress
                Material.accent: Style.progressBarWritingForegroundColor
                Material.background: Style.progressBarTrackColor
                visible: root.isWriting
            }

            // Separate skip button removed; main Next button switches label during verification
        }

        // Bottom spacer to vertically center progress section when writing/complete
        Item { Layout.fillHeight: true; visible: root.isWriting || root.isComplete }
    }
    ]
    
    // Handle next button clicks based on current state
    onNextClicked: {
        if (root.isWriting) {
            // Cancel the write
            if (root.isVerifying) {
                imageWriter.skipCurrentVerification()
            } else {
                imageWriter.cancelWrite()
                root.isWriting = false
                wizardContainer.isWriting = false
                progressText.text = qsTr("Write cancelled")
            }
        } else if (!root.isComplete) {
            // If warnings are disabled, skip the confirmation dialog
            if (imageWriter.getBoolSetting("disable_warnings")) {
                beginWriteDelay.start()
            } else {
                // Open confirmation dialog before starting
                confirmDialog.open()
            }
        } else {
            // Writing is complete, advance to next step
            wizardContainer.nextStep()
        }
    }
    
    function onFinalizing() {
        root.isVerifying = false
        progressText.text = qsTr("Finalizing...")
        progressBar.value = 100
    }

    // Confirmation dialog
    Dialog {
        id: confirmDialog
        modal: true
        parent: wizardContainer && wizardContainer.overlayRootRef ? wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton
        visible: false

        property bool allowAccept: false

        background: Rectangle {
            color: Style.mainBackgroundColor
            radius: Style.sectionBorderRadius
            border.color: Style.popupBorderColor
            border.width: Style.sectionBorderWidth
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.cardPadding
            spacing: Style.spacingMedium

            Text {
                text: qsTr("You are about to ERASE all data on: %1").arg(wizardContainer.selectedStorageName || qsTr("the storage device"))
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelErrorColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("This action is PERMANENT and CANNOT be undone.")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                color: Style.formLabelColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                Item { Layout.fillWidth: true }

                ImButton {
                    text: qsTr("Cancel")
                    onClicked: confirmDialog.close()
                }

                ImButtonRed {
                    id: acceptBtn
                    text: confirmDialog.allowAccept ? qsTr("I understand, erase and write") : qsTr("Please wait...")
                    enabled: confirmDialog.allowAccept
                    onClicked: {
                        confirmDialog.close()
                        beginWriteDelay.start()
                    }
                }
            }
        }

        // Delay accept for 2 seconds
        Timer {
            id: confirmDelay
            interval: 2000
            running: false
            repeat: false
            onTriggered: confirmDialog.allowAccept = true
        }

        // Ensure disabled before showing to avoid flicker
        onAboutToShow: {
            allowAccept = false
            confirmDelay.start()
        }
        onClosed: {
            confirmDelay.stop()
            allowAccept = false
        }
    }

    // Defer starting the write slightly until after the dialog has fully closed,
    // to avoid OS authentication prompts being cancelled by focus changes.
    Timer {
        id: beginWriteDelay
        interval: 300
        running: false
        repeat: false
        onTriggered: {
            // Ensure our window regains focus before elevating privileges
            root.forceActiveFocus()
            root.isWriting = true
            wizardContainer.isWriting = true
            progressText.text = qsTr("Starting write process...")
            progressBar.value = 0
            Qt.callLater(function(){ imageWriter.startWrite() })
        }
    }
    
    function onDownloadProgress(now, total) {
        if (root.isWriting) {
            var progress = total > 0 ? (now / total) * 100 : 0
            root.isVerifying = false
            progressBar.value = progress
            progressText.text = qsTr("Writing... %1%").arg(Math.round(progress))
            progressBar.Material.accent = Style.progressBarWritingForegroundColor
        }
    }
    
    function onVerifyProgress(now, total) {
        if (root.isWriting) {
            var progress = total > 0 ? (now / total) * 100 : 0
            root.isVerifying = true
            progressBar.value = progress
            progressText.text = qsTr("Verifying... %1%").arg(Math.round(progress))
            progressBar.Material.accent = Style.progressBarVerifyForegroundColor
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