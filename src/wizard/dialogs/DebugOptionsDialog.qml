/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../qmlcomponents"

import RpiImager

BaseDialog {
    id: popup
    
    // Override default height for this dialog
    height: Math.max(350, contentLayout ? (contentLayout.implicitHeight + Style.cardPadding * 2) : 350)
    
    // imageWriter is inherited from BaseDialog
    property var wizardContainer: null
    
    property bool initialized: false
    property bool isInitializing: false

    // Custom escape handling
    function escapePressed() {
        popup.close()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("header", function(){ 
            if (popup.imageWriter && popup.imageWriter.isScreenReaderActive()) {
                return [headerText, warningText]
            }
            return []
        }, 0)
        registerFocusGroup("options", function(){ 
            return [chkDirectIO.focusItem, chkPeriodicSync.focusItem, chkVerboseLogging.focusItem]
        }, 1)
        registerFocusGroup("buttons", function(){ 
            return [cancelButton, applyButton]
        }, 2)
    }

    // Header
    Text {
        id: headerText
        text: qsTr("Debug Options")
        font.pixelSize: Style.fontSizeLargeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        Accessible.role: Accessible.Heading
        Accessible.name: text
        Accessible.focusable: popup.imageWriter ? popup.imageWriter.isScreenReaderActive() : false
        focusPolicy: (popup.imageWriter && popup.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: popup.imageWriter ? popup.imageWriter.isScreenReaderActive() : false
    }

    // Warning text
    Text {
        id: warningText
        text: qsTr("⚠️ These options are for debugging and testing. Changing them may affect performance and data integrity.")
        font.pixelSize: Style.fontSizeDescription
        font.family: Style.fontFamily
        color: Style.formLabelErrorColor
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.focusable: popup.imageWriter ? popup.imageWriter.isScreenReaderActive() : false
        focusPolicy: (popup.imageWriter && popup.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: popup.imageWriter ? popup.imageWriter.isScreenReaderActive() : false
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

            // Section header for I/O options
            Text {
                text: qsTr("I/O Options")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.textDescriptionColor
                Layout.fillWidth: true
            }

            ImOptionPill {
                id: chkDirectIO
                text: qsTr("Enable Direct I/O (F_NOCACHE / O_DIRECT)")
                accessibleDescription: qsTr("Bypass the operating system page cache for writes. Slower but ensures data goes directly to device.")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            ImOptionPill {
                id: chkPeriodicSync
                text: qsTr("Enable Periodic Sync")
                accessibleDescription: qsTr("Periodically flush data to disk during writes. Automatically disabled when Direct I/O is active.")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            // Spacer
            Item {
                Layout.preferredHeight: Style.spacingMedium
            }

            // Section header for debugging options
            Text {
                text: qsTr("Debugging")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.textDescriptionColor
                Layout.fillWidth: true
            }

            ImOptionPill {
                id: chkVerboseLogging
                text: qsTr("Verbose Performance Logging")
                accessibleDescription: qsTr("Log detailed timing information for each write operation to help diagnose performance issues.")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            // Status display
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: statusColumn.implicitHeight + Style.spacingMedium * 2
                color: Style.titleBackgroundColor
                radius: Style.sectionBorderRadius

                ColumnLayout {
                    id: statusColumn
                    anchors.fill: parent
                    anchors.margins: Style.spacingMedium
                    spacing: Style.spacingXSmall

                    Text {
                        text: qsTr("Current Status")
                        font.pixelSize: Style.fontSizeDescription
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        color: Style.textDescriptionColor
                    }

                    Text {
                        id: statusText
                        text: {
                            var lines = [];
                            lines.push("Direct I/O: " + (chkDirectIO.checked ? "Enabled" : "Disabled"));
                            lines.push("Periodic Sync: " + (chkPeriodicSync.checked ? "Enabled" : "Disabled"));
                            if (chkDirectIO.checked && chkPeriodicSync.checked) {
                                lines.push("Note: Periodic sync is skipped when Direct I/O is active");
                            }
                            return lines.join("\n");
                        }
                        font.pixelSize: Style.fontSizeSmall
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
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
                id: cancelButton
                text: CommonStrings.cancel
                accessibleDescription: qsTr("Close the debug options dialog without saving any changes")
                Layout.minimumWidth: Style.buttonWidthMinimum
                activeFocusOnTab: true
                onClicked: {
                    popup.close();
                }
            }

            ImButtonRed {
                id: applyButton
                text: qsTr("Apply")
                accessibleDescription: qsTr("Apply the selected debug options")
                Layout.minimumWidth: Style.buttonWidthMinimum
                activeFocusOnTab: true
                onClicked: {
                    popup.applySettings();
                    popup.close();
                }
            }
        }
    }

    function initialize() {
        if (!initialized) {
            isInitializing = true;
            
            // Load current settings from ImageWriter
            chkDirectIO.checked = imageWriter.getDebugDirectIO();
            chkPeriodicSync.checked = imageWriter.getDebugPeriodicSync();
            chkVerboseLogging.checked = imageWriter.getDebugVerboseLogging();

            initialized = true;
            isInitializing = false;
            
            // Pre-compute final height before opening
            var desired = contentLayout ? (contentLayout.implicitHeight + Style.cardPadding * 2) : 350;
            popup.height = Math.max(350, desired);
        }
    }

    function applySettings() {
        // Apply settings to ImageWriter
        imageWriter.setDebugDirectIO(chkDirectIO.checked);
        imageWriter.setDebugPeriodicSync(chkPeriodicSync.checked);
        imageWriter.setDebugVerboseLogging(chkVerboseLogging.checked);
        
        console.log("Debug options applied: DirectIO=" + chkDirectIO.checked + 
                    ", PeriodicSync=" + chkPeriodicSync.checked +
                    ", VerboseLogging=" + chkVerboseLogging.checked);
    }

    onOpened: {
        initialize();
    }
    
    // Reset initialized state when closed so settings are refreshed on next open
    onClosed: {
        initialized = false;
    }
}
