/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Window 2.2
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
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

    onClosing: function(close) {
        if (wizardContainer.isWriting && !forceQuit) {
            close.accepted = false
            quitDialog.open()
        } else {
            // allow close
            close.accepted = true
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
                wizardContainer.currentStep = 0
            }
        }
    }

    // Modern error dialog (replaces legacy MsgPopup for error/info cases)
    Dialog {
        id: errorDialog
        modal: true
        parent: overlayRoot
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton

        property string titleText: qsTr("Error")
        property string message: ""

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
                Item { Layout.fillWidth: true }
                ImButton {
                    text: qsTr("Continue")
                    onClicked: errorDialog.close()
                }
            }
        }
    }

    // Specific dialog for storage removal during write
    Dialog {
        id: storageRemovedDialog
        modal: true
        parent: overlayRoot
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton

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
                Item { Layout.fillWidth: true }
                ImButtonRed {
                    text: qsTr("OK")
                    onClicked: storageRemovedDialog.close()
                }
            }
        }
    }

    // Quit dialog (modern style)
    Dialog {
        id: quitDialog
        modal: true
        parent: overlayRoot
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.cardPadding
            spacing: Style.spacingMedium

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
                Item { Layout.fillWidth: true }

                ImButton {
                    text: qsTr("No")
                    onClicked: quitDialog.close()
                }
                ImButtonRed {
                    text: qsTr("Yes")
                    onClicked: {
                        window.forceQuit = true
                        Qt.quit()
                    }
                }
            }
        }
    }

    KeychainPermissionDialog {
        id: keychainpopup
        parent: overlayRoot
        onAccepted: {
            window.imageWriter.keychainPermissionResponse(true)
        }
        onRejected: {
            window.imageWriter.keychainPermissionResponse(false)
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
    function onDownloadProgress(now,total) {
        // Forward to wizard container
        wizardContainer.onDownloadProgress(now, total)
    }

    function onVerifyProgress(now,total) {
        // Forward to wizard container
        wizardContainer.onVerifyProgress(now, total)
    }

    function onPreparationStatusUpdate(msg) {
        // Forward to wizard container
        wizardContainer.onPreparationStatusUpdate(msg)
    }

    function onError(msg) {
        errorDialog.titleText = qsTr("Error")
        errorDialog.message = msg
        errorDialog.open()
    }

    function onFinalizing() {
        wizardContainer.onFinalizing()
    }

    function onNetworkInfo(msg) {
        if (imageWriter.isEmbeddedMode() && wizardContainer) {
            wizardContainer.networkInfoText = msg
        }
    }

    // Called from C++ when selected device is removed
    function onSelectedDeviceRemoved() {
        if (wizardContainer) {
            wizardContainer.selectedStorageName = ""
        }
        imageWriter.setDst("")

        // If we are past storage selection, navigate back there
        if (wizardContainer && wizardContainer.currentStep > wizardContainer.stepStorageSelection) {
            wizardContainer.jumpToStep(wizardContainer.stepStorageSelection)
        }

        // Inform the user with the dedicated modern dialog
        storageRemovedDialog.open()
    }

    // Called from C++ when a write was cancelled because the storage device was removed
    function onWriteCancelledDueToDeviceRemoval() {
        if (wizardContainer) {
            wizardContainer.isWriting = false
            wizardContainer.selectedStorageName = ""
        }
        // Clear backend dst reference
        window.imageWriter.setDst("")
        // Navigate back to storage selection for safety
        if (wizardContainer) wizardContainer.jumpToStep(wizardContainer.stepStorageSelection)
        // Show dedicated dialog
        storageRemovedDialog.open()
    }

    function onKeychainPermissionRequested() {
        keychainpopup.askForPermission()
    }
}
