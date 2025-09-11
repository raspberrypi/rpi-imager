/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtCore
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "../../qmlcomponents"

import RpiImager

Dialog {
    id: popup
    width: 520
    height: 280
    
    // Center the dialog on screen
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    
    modal: true
    closePolicy: Popup.CloseOnEscape
    
    required property ImageWriter imageWriter
    // Optional reference to the wizard container for ephemeral flags
    property var wizardContainer: null
    
    property bool initialized: false
    property url selectedRepo: ""
    
    // Custom modal overlay background
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.3)
        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }
    }
    
    // Remove standard dialog buttons since we have custom ones
    standardButtons: Dialog.NoButton
    
    // Set the dialog background directly
    background: Rectangle {
        color: Style.titleBackgroundColor
        radius: Style.sectionBorderRadius
        border.color: Style.popupBorderColor
        border.width: Style.sectionBorderWidth
    }

    Connections {
        target: imageWriter
        // Handle native file selection for "Use custom"
        function onFileSelected(fileUrl) {
            popup.selectedRepo = fileUrl
        }
    }
    
    // Main content - direct ColumnLayout without Rectangle wrapper
    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: Style.cardPadding
        spacing: Style.spacingLarge
        
        // Header
        Text {
            text: qsTr("App Options")
            font.pixelSize: Style.fontSizeLargeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
        
        // Options section
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: optionsLayout.implicitHeight + Style.cardPadding
            
            ColumnLayout {
                id: optionsLayout
                anchors.fill: parent
                anchors.margins: Style.cardPadding
                spacing: Style.spacingMedium
                
                ImOptionPill {
                    id: chkBeep
                    text: qsTr("Play sound when finished")
                    Layout.fillWidth: true
                }
                
                ImOptionPill {
                    id: chkEject
                    text: qsTr("Eject media when finished")
                    Layout.fillWidth: true
                }
                
                ImOptionPill {
                    id: chkTelemetry
                    text: qsTr("Enable anonymous statistics (telemetry)")
                    helpLabel: imageWriter.isEmbeddedMode() ? "" : qsTr("What is this?")
                    helpUrl: imageWriter.isEmbeddedMode() ? "" : "https://github.com/raspberrypi/rpi-imager?tab=readme-ov-file#telemetry"
                    Layout.fillWidth: true
                }

                ImOptionPill {
                    id: chkDisableWarnings
                    text: qsTr("Disable warnings")
                    Layout.fillWidth: true
                    onCheckedChanged: {
                        if (checked) {
                            // Confirm before enabling this risky setting
                            confirmDisableWarnings.open()
                        } else if (popup.wizardContainer) {
                            popup.wizardContainer.disableWarnings = false
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingMedium
                    
                    ImTextField {
                        id: fieldCustomRepository
                        text: selectedRepo !== "" ? UrlFmt.display(selectedRepo) : ""
                        Layout.fillWidth: true
                        placeholderText: qsTr("Select custom Repository")
                        font.pixelSize: Style.fontSizeInput
                        readOnly: true
                    }
                    
                    ImButton {
                        id: browseButton
                        text: qsTr("Browse")
                        Layout.minimumWidth: 80
                        onClicked: {
                            // Prefer native file dialog via Imager's wrapper, but only if available
                            if (imageWriter.nativeFileDialogAvailable()) {
                                // Defer opening the native dialog until after the current event completes
                                Qt.callLater(function() {
                                    imageWriter.openFileDialog(
                                        qsTr("Select Repository"),
                                        CommonStrings.repoFiltersString)
                                })
                            } else {
                                // Fallback to QML dialog (forced non-native)
                                repoFileDialog.open()
                            }
                        }
                    }
                }
            }
        }
        // Spacer
        Item {
            Layout.fillHeight: true
        }
        // Buttons section with background
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: buttonRow.implicitHeight + Style.cardPadding
            color: Style.titleBackgroundColor
            
            RowLayout {
                id: buttonRow
                anchors.fill: parent
                anchors.margins: Style.cardPadding / 2
                spacing: Style.spacingMedium
                
                Item {
                    Layout.fillWidth: true
                }
                
                ImButton {
                    text: qsTr("Cancel")
                    Layout.minimumWidth: Style.buttonWidthMinimum
                    onClicked: {
                        popup.close()
                    }
                }
                
                ImButtonRed {
                    text: qsTr("Save")
                    Layout.minimumWidth: Style.buttonWidthMinimum
                    onClicked: {
                        popup.applySettings()
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
            // Do not load from QSettings; keep ephemeral
            chkDisableWarnings.checked = popup.wizardContainer ? popup.wizardContainer.disableWarnings : false
            if (imageWriter.customRepo()) {
                selectedRepo = imageWriter.constantOsListUrl()
            }

            initialized = true
            // Pre-compute final height before opening to avoid first-show reflow
            var desired = contentLayout ? (contentLayout.implicitHeight + Style.cardPadding * 2) : 280
            popup.height = Math.max(280, desired)
        }
    }
    
    function applySettings() {
        // Save settings to ImageWriter
        imageWriter.setSetting("beep", chkBeep.checked)
        imageWriter.setSetting("eject", chkEject.checked)
        imageWriter.setSetting("telemetry", chkTelemetry.checked)
        // Do not persist disable_warnings; set ephemeral flag only
        if (popup.wizardContainer) popup.wizardContainer.disableWarnings = chkDisableWarnings.checked
        if (popup.selectedRepo !== "") {
            imageWriter.refreshOsListFrom(selectedRepo)
        }
    }
    
    onOpened: {
        initialize()
        chkBeep.forceActiveFocus()
    }

    // Confirmation dialog for disabling warnings
    Dialog {
        id: confirmDisableWarnings
        modal: true
        parent: popup.contentItem
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton

        onClosed: {
            // If dialog was closed without confirming, revert the toggle
            if (!confirmAccepted) {
                chkDisableWarnings.checked = false
            }
            confirmAccepted = false
        }

        property bool confirmAccepted: false

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
                text: qsTr("Disable warnings?")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
            }

            Text {
                textFormat: Text.StyledText
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeDescription
                font.family: Style.fontFamily
                color: Style.textDescriptionColor
                Layout.fillWidth: true
                text: qsTr("If you disable warnings, Raspberry Pi Imager will <b>not show confirmation prompts before writing images</b>. You will still be required to <b>type the exact name</b> when selecting a system drive.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                Item { Layout.fillWidth: true }

                ImButton {
                    text: qsTr("Cancel")
                    onClicked: confirmDisableWarnings.close()
                }

                ImButtonRed {
                    text: qsTr("Disable warnings")
                    onClicked: {
                        confirmDisableWarnings.confirmAccepted = true
                        if (popup.wizardContainer) popup.wizardContainer.disableWarnings = true
                        confirmDisableWarnings.close()
                    }
                }
            }
        }
    }

    property alias repoFileDialog: repoFileDialog

    ImFileDialog {
        id: repoFileDialog
        dialogTitle: qsTr("Select custom repository")
        nameFilters: CommonStrings.repoFiltersList
        onAccepted: {
            popup.selectedRepo = selectedFile
        }
        onRejected: {
            // No-op; user cancelled
        }
    }
}

