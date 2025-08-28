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
    
    // Main content - direct ColumnLayout without Rectangle wrapper
    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: Style.cardPadding
        spacing: Style.spacingLarge
        
        // Header
        Text {
            text: qsTr("Advanced Options")
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
            initialized = true
        }
    }
    
    function applySettings() {
        // Save settings to ImageWriter
        imageWriter.setSetting("beep", chkBeep.checked)
        imageWriter.setSetting("eject", chkEject.checked)
        imageWriter.setSetting("telemetry", chkTelemetry.checked)
        // Do not persist disable_warnings; set ephemeral flag only
        if (popup.wizardContainer) popup.wizardContainer.disableWarnings = chkDisableWarnings.checked
    }
    
    onOpened: {
        initialize()
        chkBeep.forceActiveFocus()
        // Ensure the dialog is tall enough to show buttons
        popup.height = Math.max(popup.height, contentLayout ? (contentLayout.implicitHeight + Style.cardPadding * 2) : 280)
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
}