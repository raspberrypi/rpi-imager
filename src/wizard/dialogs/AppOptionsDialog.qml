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

BaseDialog {
    id: popup
    
    // Override default height for this more complex dialog
    height: Math.max(280, contentLayout ? (contentLayout.implicitHeight + Style.cardPadding * 2) : 280)
    
    required property ImageWriter imageWriter
    // Optional reference to the wizard container for ephemeral flags
    property var wizardContainer: null
    
    property bool initialized: false
    property url selectedRepo: ""
    property url originalRepo: ""

    // Custom escape handling
    function escapePressed() {
        popup.close()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("options", function(){ 
            return [chkBeep.focusItem, chkEject.focusItem, chkTelemetry.focusItem, chkDisableWarnings.focusItem]
        }, 0)
        registerFocusGroup("repository", function(){ 
            return [fieldCustomRepository, browseButton]
        }, 1)
        registerFocusGroup("buttons", function(){ 
            return [cancelButton, saveButton]
        }, 2)
    }

    Connections {
        target: imageWriter
        // Handle native file selection for "Use custom"
        function onFileSelected(fileUrl) {
            popup.selectedRepo = fileUrl;
        }
    }

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
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            ImOptionPill {
                id: chkEject
                text: qsTr("Eject media when finished")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            ImOptionPill {
                id: chkTelemetry
                text: qsTr("Enable anonymous statistics (telemetry)")
                helpLabel: imageWriter.isEmbeddedMode() ? "" : qsTr("What is this?")
                helpUrl: imageWriter.isEmbeddedMode() ? "" : "https://github.com/raspberrypi/rpi-imager?tab=readme-ov-file#anonymous-metrics-telemetry"
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            ImOptionPill {
                id: chkDisableWarnings
                text: qsTr("Disable warnings")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
                onCheckedChanged: {
                    if (checked) {
                        // Confirm before enabling this risky setting
                        confirmDisableWarnings.open();
                    } else if (popup.wizardContainer) {
                        popup.wizardContainer.disableWarnings = false;
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
                    activeFocusOnTab: true
                }

                ImButton {
                    id: browseButton
                    text: qsTr("Browse")
                    Layout.minimumWidth: 80
                    activeFocusOnTab: true
                    onClicked: {
                        // Prefer native file dialog via Imager's wrapper, but only if available
                        if (imageWriter.nativeFileDialogAvailable()) {
                            // Defer opening the native dialog until after the current event completes
                            Qt.callLater(function () {
                                imageWriter.openFileDialog(qsTr("Select Repository"), CommonStrings.repoFiltersString);
                            });
                        } else {
                            // Fallback to QML dialog (forced non-native)
                            repoFileDialog.open();
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
                id: cancelButton
                text: qsTr("Cancel")
                Layout.minimumWidth: Style.buttonWidthMinimum
                activeFocusOnTab: true
                onClicked: {
                    popup.close();
                }
            }

            ImButtonRed {
                id: saveButton
                text: qsTr("Save")
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
            // Load current settings from ImageWriter
            chkBeep.checked = imageWriter.getBoolSetting("beep");
            chkEject.checked = imageWriter.getBoolSetting("eject");
            chkTelemetry.checked = imageWriter.getBoolSetting("telemetry");
            // Do not load from QSettings; keep ephemeral
            chkDisableWarnings.checked = popup.wizardContainer ? popup.wizardContainer.disableWarnings : false;
            if (imageWriter.customRepo()) {
                selectedRepo = imageWriter.osListUrl();
                originalRepo = selectedRepo;
            } else {
                selectedRepo = "";
                originalRepo = "";
            }

            initialized = true;
            // Pre-compute final height before opening to avoid first-show reflow
            var desired = contentLayout ? (contentLayout.implicitHeight + Style.cardPadding * 2) : 280;
            popup.height = Math.max(280, desired);
        }
    }

    function applySettings() {
        // Save settings to ImageWriter
        imageWriter.setSetting("beep", chkBeep.checked);
        imageWriter.setSetting("eject", chkEject.checked);
        imageWriter.setSetting("telemetry", chkTelemetry.checked);
        // Do not persist disable_warnings; set ephemeral flag only
        if (popup.wizardContainer)
            popup.wizardContainer.disableWarnings = chkDisableWarnings.checked;
        // Only save repository setting if it has actually changed
        if (popup.selectedRepo !== popup.originalRepo) {
            if (popup.selectedRepo !== "") {
                imageWriter.refreshOsListFrom(selectedRepo);
            } else {
                // User cleared the repository - reset to default
                // Note: setCustomRepo with the default URL will reset to default behavior
                imageWriter.setCustomRepo(Qt.resolvedUrl("https://downloads.raspberrypi.org/os_list_imagingutility_v4.json"));
            }
        }
    }

    onOpened: {
        initialize();
        // BaseDialog handles the focus management automatically
    }

    // Confirmation dialog for disabling warnings
    BaseDialog {
        id: confirmDisableWarnings
        parent: popup.contentItem
        anchors.centerIn: parent

        onClosed: {
            // If dialog was closed without confirming, revert the toggle
            if (!confirmAccepted) {
                chkDisableWarnings.checked = false;
            }
            confirmAccepted = false;
        }

        property bool confirmAccepted: false

        // Custom escape handling
        function escapePressed() {
            confirmDisableWarnings.close()
        }

        // Register focus groups when component is ready
        Component.onCompleted: {
            registerFocusGroup("buttons", function(){ 
                return [confirmCancelButton, confirmDisableButton] 
            }, 0)
        }

        // Dialog content
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
            Item {
                Layout.fillWidth: true
            }

            ImButton {
                id: confirmCancelButton
                text: qsTr("Cancel")
                activeFocusOnTab: true
                onClicked: confirmDisableWarnings.close()
            }

            ImButtonRed {
                id: confirmDisableButton
                text: qsTr("Disable warnings")
                activeFocusOnTab: true
                onClicked: {
                    confirmDisableWarnings.confirmAccepted = true;
                    if (popup.wizardContainer)
                        popup.wizardContainer.disableWarnings = true;
                    confirmDisableWarnings.close();
                }
            }
        }
    }

    property alias repoFileDialog: repoFileDialog

    ImFileDialog {
        id: repoFileDialog
        parent: popup.parent  // Inherit parent from AppOptionsDialog (which is overlayRoot)
        anchors.centerIn: parent
        dialogTitle: qsTr("Select custom repository")
        nameFilters: CommonStrings.repoFiltersList
        onAccepted: {
            popup.selectedRepo = selectedFile;
        }
        onRejected:
        // No-op; user cancelled
        {}
    }
}