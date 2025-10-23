/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import "qmlcomponents"
import "wizard"

import RpiImager

ApplicationWindow {
    id: window
    visible: true

    required property ImageWriter imageWriter
    // Whether to show the landing Language Selection step (set from C++)
    property bool showLanguageSelection: false
    // Wizard manages drive list and selection state
    property bool forceQuit: false
    // Expose overlay root to child components for dialog parenting
    readonly property alias overlayRootItem: overlayRoot

    width: imageWriter.isEmbeddedMode() ? -1 : 680
    height: imageWriter.isEmbeddedMode() ? -1 : 450
    minimumWidth: imageWriter.isEmbeddedMode() ? -1 : 680
    minimumHeight: imageWriter.isEmbeddedMode() ? -1 : 420

    title: qsTr("Raspberry Pi Imager v%1").arg(imageWriter.constantVersion())

    Component.onCompleted: {
        // Set the main window for modal file dialogs
        imageWriter.setMainWindow(window)
    }

    onClosing: function (close) {
        if (wizardContainer.isWriting && !forceQuit) {
            close.accepted = false;
            quitDialog.open();
        } else {
            // allow close
            close.accepted = true;
        }
    }

    // Global overlay to parent/center dialogs across the whole window
    Item {
        id: overlayRoot
        anchors.fill: parent
        z: 1000
    }

    // Main wizard interface
    Rectangle {
        id: wizardBackground
        anchors.fill: parent
        color: Style.mainBackgroundColor

        WizardContainer {
            id: wizardContainer
            anchors.fill: parent
            imageWriter: window.imageWriter
            optionsPopup: appOptionsDialog
            overlayRootRef: overlayRoot
            // Show Language step if C++ requested it
            showLanguageSelection: window.showLanguageSelection

            onWizardCompleted: {
                // Reset to start of wizard or close application
                wizardContainer.currentStep = 0;
            }
        }
    }

    // Modern error dialog (replaces legacy MsgPopup for error/info cases)
    BaseDialog {
        id: errorDialog
        parent: overlayRoot
        anchors.centerIn: parent

        property string titleText: qsTr("Error")
        property string message: ""

        // Custom escape handling
        function escapePressed() {
            errorDialog.close()
        }

        // Register focus groups when component is ready
        Component.onCompleted: {
            registerFocusGroup("buttons", function(){ 
                return [errorContinueButton] 
            }, 0)
        }

        // Dialog content
        Text {
            text: errorDialog.titleText
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        Text {
            text: errorDialog.message
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeDescription
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item {
                Layout.fillWidth: true
            }
            ImButton {
                id: errorContinueButton
                text: CommonStrings.continueText
                accessibleDescription: qsTr("Close the error dialog and continue")
                activeFocusOnTab: true
                onClicked: errorDialog.close()
            }
        }
    }

    // Specific dialog for storage removal during write
    BaseDialog {
        id: storageRemovedDialog
        parent: overlayRoot
        anchors.centerIn: parent

        // Custom escape handling
        function escapePressed() {
            storageRemovedDialog.close()
        }

        // Register focus groups when component is ready
        Component.onCompleted: {
            registerFocusGroup("buttons", function(){ 
                return [storageOkButton] 
            }, 0)
        }

        // Dialog content
        Text {
            text: qsTr("Storage device removed")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        Text {
            text: qsTr("The storage device was removed while writing, so the operation was cancelled. Please reinsert the device or select a different one to continue.")
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeDescription
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item {
                Layout.fillWidth: true
            }
            ImButtonRed {
                id: storageOkButton
                text: qsTr("OK")
                accessibleDescription: qsTr("Close the storage removed notification and return to storage selection")
                activeFocusOnTab: true
                onClicked: storageRemovedDialog.close()
            }
        }
    }

    // Quit dialog (modern style)
    BaseDialog {
        id: quitDialog
        parent: overlayRoot
        anchors.centerIn: parent

        // Custom escape handling
        function escapePressed() {
            quitDialog.close()
        }

        // Register focus groups when component is ready
        Component.onCompleted: {
            registerFocusGroup("buttons", function(){ 
                return [quitNoButton, quitYesButton] 
            }, 0)
        }

        // Dialog content
        Text {
            text: qsTr("Are you sure you want to quit?")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        Text {
            text: qsTr("Raspberry Pi Imager is still busy. Are you sure you want to quit?")
            font.pixelSize: Style.fontSizeDescription
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item {
                Layout.fillWidth: true
            }

            ImButton {
                id: quitNoButton
                text: CommonStrings.no
                accessibleDescription: qsTr("Return to Raspberry Pi Imager and continue the current operation")
                activeFocusOnTab: true
                onClicked: quitDialog.close()
            }
            ImButtonRed {
                id: quitYesButton
                text: CommonStrings.yes
                accessibleDescription: qsTr("Force quit Raspberry Pi Imager and cancel the current write operation")
                activeFocusOnTab: true
                onClicked: {
                    window.forceQuit = true;
                    Qt.quit();
                }
            }
        }
    }

    KeychainPermissionDialog {
        id: keychainpopup
        parent: overlayRoot
        onAccepted: {
            window.imageWriter.keychainPermissionResponse(true);
        }
        onRejected: {
            window.imageWriter.keychainPermissionResponse(false);
        }
    }

    UpdateAvailableDialog {
        id: updatepopup
        // parent can be set to overlayRoot if needed for centering above
        parent: overlayRoot
        onAccepted: {}
        onRejected: {}
    }

    AppOptionsDialog {
        id: appOptionsDialog
        parent: overlayRoot
        imageWriter: window.imageWriter
        wizardContainer: wizardContainer
    }

    // Removed embeddedFinishedPopup; handled by Wizard Done step

    /* Slots for signals imagewrite emits */
    function onDownloadProgress(now, total) {
        // Forward to wizard container
        wizardContainer.onDownloadProgress(now, total);
    }

    function onVerifyProgress(now, total) {
        // Forward to wizard container
        wizardContainer.onVerifyProgress(now, total);
    }

    function onPreparationStatusUpdate(msg) {
        // Forward to wizard container
        wizardContainer.onPreparationStatusUpdate(msg);
    }

    function onError(msg) {
        errorDialog.titleText = qsTr("Error");
        errorDialog.message = msg;
        errorDialog.open();
    }

    function onFinalizing() {
        wizardContainer.onFinalizing();
    }

    function onNetworkInfo(msg) {
        if (imageWriter.isEmbeddedMode() && wizardContainer) {
            wizardContainer.networkInfoText = msg;
        }
    }

    // Called from C++ when selected device is removed
    function onSelectedDeviceRemoved() {
        if (wizardContainer) {
            wizardContainer.selectedStorageName = "";
        }
        imageWriter.setDst("");

        // If we are past storage selection, navigate back there
        if (wizardContainer && wizardContainer.currentStep > wizardContainer.stepStorageSelection) {
            wizardContainer.jumpToStep(wizardContainer.stepStorageSelection);
        }

        // Inform the user with the dedicated modern dialog
        storageRemovedDialog.open();
    }

    // Called from C++ when a write was cancelled because the storage device was removed
    function onWriteCancelledDueToDeviceRemoval() {
        if (wizardContainer) {
            wizardContainer.isWriting = false;
            wizardContainer.selectedStorageName = "";
        }
        // Clear backend dst reference
        window.imageWriter.setDst("");
        // Navigate back to storage selection for safety
        if (wizardContainer)
            wizardContainer.jumpToStep(wizardContainer.stepStorageSelection);
        // Show dedicated dialog
        storageRemovedDialog.open();
    }

    function onKeychainPermissionRequested() {
        // If warnings are disabled, automatically grant permission without showing dialog
        if (wizardContainer.disableWarnings) {
            window.imageWriter.keychainPermissionResponse(true);
        } else {
            keychainpopup.askForPermission();
        }
    }
}
