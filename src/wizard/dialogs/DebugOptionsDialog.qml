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
    
    // Height based on window size minus padding - content scrolls within
    height: parent ? Math.min(500, parent.height - Style.cardPadding * 2) : 500
    
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
            return [chkDirectIO.focusItem, chkAsyncIO.focusItem, chkPeriodicSync.focusItem, chkVerboseLogging.focusItem, chkIPv4Only.focusItem, chkSkipEndOfDevice.focusItem]
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

    // Scrollable options section
    ScrollView {
        id: scrollView
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth  // Ensure content uses full width
        
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: optionsLayout
            width: scrollView.availableWidth  // Use ScrollView's available width directly
            spacing: Style.spacingMedium

            // Section header for I/O options
            Text {
                text: qsTr("I/O Options")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.textDescriptionColor
                Layout.fillWidth: true
                Layout.topMargin: Style.spacingSmall
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
                id: chkAsyncIO
                text: qsTr("Enable Async I/O")
                accessibleDescription: qsTr("Queue multiple writes to overlap device latency. Improves performance with Direct I/O enabled.")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }
            
            // Async queue depth slider (only visible when async is enabled)
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Style.spacingLarge
                visible: chkAsyncIO.checked
                spacing: Style.spacingSmall
                
                Text {
                    text: qsTr("Queue Depth:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                }
                
                Slider {
                    id: asyncQueueDepthSlider
                    Layout.fillWidth: true
                    from: 1
                    to: 512  // Max supported by ring buffer - high values mainly benefit NVMe/USB4
                    stepSize: 1
                    value: 16
                    
                    // Snap to power-of-2 and convenient values for nice display
                    property var snapPoints: [1, 2, 4, 8, 16, 32, 64, 128, 256, 384, 512]
                    
                    onMoved: {
                        // Snap to nearest good value
                        var closest = snapPoints[0];
                        var minDist = Math.abs(value - closest);
                        for (var i = 1; i < snapPoints.length; i++) {
                            var dist = Math.abs(value - snapPoints[i]);
                            if (dist < minDist) {
                                minDist = dist;
                                closest = snapPoints[i];
                            }
                        }
                        value = closest;
                    }
                    
                    Accessible.role: Accessible.Slider
                    Accessible.name: qsTr("Async queue depth: %1").arg(Math.round(value))
                }
                
                Text {
                    text: Math.round(asyncQueueDepthSlider.value)
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.minimumWidth: 40
                    horizontalAlignment: Text.AlignRight
                }
            }
            
            // Memory usage estimate
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Style.spacingLarge
                visible: chkAsyncIO.checked
                text: qsTr("Buffer memory: ~%1-%2 MB (varies by system RAM)").arg(Math.round(asyncQueueDepthSlider.value * 1)).arg(Math.round(asyncQueueDepthSlider.value * 8))
                font.pixelSize: Style.fontSizeSmall
                font.family: Style.fontFamily
                color: Style.textDescriptionColor
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

            // Spacer
            Item {
                Layout.preferredHeight: Style.spacingMedium
            }

            // Section header for network options
            Text {
                text: qsTr("Network Options")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.textDescriptionColor
                Layout.fillWidth: true
            }

            ImOptionPill {
                id: chkIPv4Only
                text: qsTr("Force IPv4-only Downloads")
                accessibleDescription: qsTr("Only use IPv4 for downloads. Enable this if you experience connection issues due to broken IPv6 routing.")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            // Spacer
            Item {
                Layout.preferredHeight: Style.spacingMedium
            }

            // Section header for workarounds
            Text {
                text: qsTr("Workarounds")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.textDescriptionColor
                Layout.fillWidth: true
            }

            ImOptionPill {
                id: chkSkipEndOfDevice
                text: qsTr("Counterfeit Card Mode (skip end-of-device checks)")
                accessibleDescription: qsTr("Skip operations at the end of the storage device. Enable this for counterfeit SD cards that report a fake larger capacity. The image must be smaller than the card's real capacity.")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            // Warning for counterfeit mode
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Style.spacingLarge
                visible: chkSkipEndOfDevice.checked
                text: qsTr("⚠️ Only enable this if your SD card reports a larger capacity than it actually has. Make sure your image is smaller than the card's real capacity!")
                font.pixelSize: Style.fontSizeSmall
                font.family: Style.fontFamily
                color: Style.formLabelErrorColor
                wrapMode: Text.WordWrap
            }

            // Status display
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: statusColumn.implicitHeight + Style.spacingMedium * 2
                Layout.bottomMargin: Style.spacingSmall
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
                            var depth = Math.round(asyncQueueDepthSlider.value);
                            lines.push("Direct I/O: " + (chkDirectIO.checked ? "Enabled" : "Disabled"));
                            lines.push("Async I/O: " + (chkAsyncIO.checked ? "Enabled (depth " + depth + ", ~" + depth + "-" + (depth * 8) + " MB)" : "Disabled"));
                            lines.push("Periodic Sync: " + (chkPeriodicSync.checked ? "Enabled" : "Disabled"));
                            lines.push("IPv4-only: " + (chkIPv4Only.checked ? "Enabled" : "Disabled"));
                            lines.push("Counterfeit Card Mode: " + (chkSkipEndOfDevice.checked ? "Enabled" : "Disabled"));
                            if (chkDirectIO.checked && chkAsyncIO.checked) {
                                lines.push("✓ Optimal: Direct I/O + Async I/O for best performance");
                            } else if (chkDirectIO.checked) {
                                lines.push("⚠ Consider enabling Async I/O to improve Direct I/O performance");
                            }
                            if (chkDirectIO.checked && chkPeriodicSync.checked) {
                                lines.push("Note: Periodic sync is skipped when Direct I/O is active");
                            }
                            if (chkIPv4Only.checked) {
                                lines.push("Note: Image downloads will use IPv4 only (OS list fetching unaffected)");
                            }
                            if (chkSkipEndOfDevice.checked) {
                                lines.push("⚠ Counterfeit mode: Skipping end-of-device checks");
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
            chkAsyncIO.checked = imageWriter.getDebugAsyncIO();
            asyncQueueDepthSlider.value = imageWriter.getDebugAsyncQueueDepth();
            chkPeriodicSync.checked = imageWriter.getDebugPeriodicSync();
            chkVerboseLogging.checked = imageWriter.getDebugVerboseLogging();
            chkIPv4Only.checked = imageWriter.getDebugIPv4Only();
            chkSkipEndOfDevice.checked = imageWriter.getDebugSkipEndOfDevice();

            initialized = true;
            isInitializing = false;
        }
    }

    function applySettings() {
        // Apply settings to ImageWriter
        imageWriter.setDebugDirectIO(chkDirectIO.checked);
        imageWriter.setDebugAsyncIO(chkAsyncIO.checked);
        imageWriter.setDebugAsyncQueueDepth(Math.round(asyncQueueDepthSlider.value));
        imageWriter.setDebugPeriodicSync(chkPeriodicSync.checked);
        imageWriter.setDebugVerboseLogging(chkVerboseLogging.checked);
        imageWriter.setDebugIPv4Only(chkIPv4Only.checked);
        imageWriter.setDebugSkipEndOfDevice(chkSkipEndOfDevice.checked);
        
        console.log("Debug options applied: DirectIO=" + chkDirectIO.checked + 
                    ", AsyncIO=" + chkAsyncIO.checked +
                    ", AsyncQueueDepth=" + Math.round(asyncQueueDepthSlider.value) +
                    ", PeriodicSync=" + chkPeriodicSync.checked +
                    ", VerboseLogging=" + chkVerboseLogging.checked +
                    ", IPv4Only=" + chkIPv4Only.checked +
                    ", SkipEndOfDevice=" + chkSkipEndOfDevice.checked);
    }

    onOpened: {
        initialize();
    }
    
    // Reset initialized state when closed so settings are refreshed on next open
    onClosed: {
        initialized = false;
    }
}
