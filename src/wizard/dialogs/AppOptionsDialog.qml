/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
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

    // Custom escape handling
    function escapePressed() {
        popup.close()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("options", function(){ 
            return [chkBeep.focusItem, chkEject.focusItem, chkTelemetry.focusItem,
                    chkDisableWarnings.focusItem, editRepoButton.focusItem, secureBootKeyButton.focusItem]
        }, 0)
        registerFocusGroup("buttons", function(){ 
            return [cancelButton, saveButton]
        }, 2)
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
        Accessible.role: Accessible.Heading
        Accessible.name: text
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
                accessibleDescription: qsTr("Play an audio notification when the image write process completes")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            ImOptionPill {
                id: chkEject
                text: qsTr("Eject media when finished")
                accessibleDescription: qsTr("Automatically eject the storage device when the write process completes successfully")
                Layout.fillWidth: true
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
            }

            ImOptionPill {
                id: chkTelemetry
                text: qsTr("Enable anonymous statistics (telemetry)")
                accessibleDescription: qsTr("Send anonymous usage statistics to help improve Raspberry Pi Imager")
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
                accessibleDescription: qsTr("Skip confirmation dialogs before writing images (advanced users only)")
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

            ImOptionButton {
                id: editRepoButton
                text: qsTr("Content Repository")
                btnText: qsTr("Edit")
                accessibleDescription: qsTr("Change the source of operating system images between official Raspberry Pi repository and custom sources")
                Layout.fillWidth: true
                // Disable while write is in progress to prevent changing source during write
                enabled: imageWriter.writeState === ImageWriter.Idle ||
                         imageWriter.writeState === ImageWriter.Succeeded ||
                         imageWriter.writeState === ImageWriter.Failed ||
                         imageWriter.writeState === ImageWriter.Cancelled
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
                onClicked: {
                    if (!repoDialog.wizardContainer) {
                        repoDialog.wizardContainer = popup.wizardContainer
                    }
                    popup.close()
                    Qt.callLater(function () {
                        repoDialog.open()
                    });
                }
            }

            ImOptionButton {
                id: secureBootKeyButton
                text: qsTr("Secure Boot RSA Key")
                btnText: rsaKeyPath.text ? qsTr("Change") : qsTr("Select")
                accessibleDescription: qsTr("Select an RSA 2048-bit private key for signing boot images in secure boot mode")
                Layout.fillWidth: true
                // Disable while write is in progress
                enabled: imageWriter.writeState === ImageWriter.Idle ||
                         imageWriter.writeState === ImageWriter.Succeeded ||
                         imageWriter.writeState === ImageWriter.Failed ||
                         imageWriter.writeState === ImageWriter.Cancelled
                Component.onCompleted: {
                    focusItem.activeFocusOnTab = true
                }
                onClicked: {
                    var keyPath = imageWriter.getNativeOpenFileName(
                        qsTr("Select RSA Private Key"), 
                        "", 
                        qsTr("PEM Files (*.pem);;All Files (*)")
                    );
                    if (keyPath) {
                        rsaKeyPath.text = keyPath;
                    }
                }
                
                Text {
                    id: rsaKeyPath
                    text: ""
                    visible: false
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
                accessibleDescription: qsTr("Close the options dialog without saving any changes")
                Layout.minimumWidth: Style.buttonWidthMinimum
                activeFocusOnTab: true
                onClicked: {
                    popup.close();
                }
            }

            ImButtonRed {
                id: saveButton
                text: qsTr("Save")
                accessibleDescription: qsTr("Save the selected options and apply them to Raspberry Pi Imager")
                Layout.minimumWidth: Style.buttonWidthMinimum
                activeFocusOnTab: true
                onClicked: {
                    popup.applySettings();
                    popup.close();
                }
            }
        }
    }

    RepositoryDialog {
        id: repoDialog
        parent: popup.parent
        imageWriter: popup.imageWriter
        wizardContainer: popup.wizardContainer
    }

    function initialize() {
        if (!initialized) {
            // Load current settings from ImageWriter
            chkBeep.checked = imageWriter.getBoolSetting("beep");
            chkEject.checked = imageWriter.getBoolSetting("eject");
            chkTelemetry.checked = imageWriter.getBoolSetting("telemetry");
            // Do not load from QSettings; keep ephemeral
            chkDisableWarnings.checked = popup.wizardContainer ? popup.wizardContainer.disableWarnings : false;
            // Load secure boot RSA key path
            var keyPath = imageWriter.getStringSetting("secureboot_rsa_key");
            if (keyPath) {
                rsaKeyPath.text = keyPath;
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
        imageWriter.setSetting("secureboot_rsa_key", rsaKeyPath.text);
        // Do not persist disable_warnings; set ephemeral flag only
        if (popup.wizardContainer)
            popup.wizardContainer.disableWarnings = chkDisableWarnings.checked;
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
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Text {
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeDescription
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            Layout.fillWidth: true
            text: qsTr("If you disable warnings, Raspberry Pi Imager will <b>not show confirmation prompts before writing images</b>. You will still be required to <b>type the exact name</b> when selecting a system drive.")
            Accessible.role: Accessible.StaticText
            Accessible.name: text.replace(/<[^>]+>/g, '')  // Strip HTML tags for accessibility
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item {
                Layout.fillWidth: true
            }

            ImButton {
                id: confirmCancelButton
                text: CommonStrings.cancel
                accessibleDescription: qsTr("Keep warnings enabled and return to the options dialog")
                activeFocusOnTab: true
                onClicked: confirmDisableWarnings.close()
            }

            ImButtonRed {
                id: confirmDisableButton
                text: qsTr("Disable warnings")
                accessibleDescription: qsTr("Disable confirmation prompts before writing images, requiring only exact name entry for system drives")
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
}
