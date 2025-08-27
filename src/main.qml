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
    readonly property DriveListModel driveListModel: imageWriter.getDriveList()
    
    property string selectedOsName: ""
    property string selectedStorageName: ""
    property bool isCacheVerifying: false
    property bool forceQuit: false

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
    
    Shortcut {
        sequence: StandardKey.Quit
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!progressBar.visible) {
                Qt.quit()
            }
        }
    }

    Shortcut {
        sequences: ["Shift+Ctrl+X", "Shift+Meta+X"]
        context: Qt.ApplicationShortcut
        onActivated: {
            advancedOptionsPopup.initialize()
            advancedOptionsPopup.show()
        }
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
            optionsPopup: advancedOptionsPopup
            
            onWizardCompleted: {
                // Reset to start of wizard or close application
                wizardContainer.currentStep = 0
            }
        }
    }

    // Popups are no longer needed with the wizard interface

    MsgPopup {
        id: msgpopup
        onOpened: {
            forceActiveFocus()
        }
        onClosed: {
            window.imageWriter.setDst("")
            window.selectedStorageName = ""
            window.resetWriteButton()
        }
    }

    // Modern error dialog (replaces legacy MsgPopup for error/info cases)
    Dialog {
        id: errorDialog
        modal: true
        parent: window.contentItem
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton

        property string titleText: qsTr("Error")
        property string message: ""

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
        parent: window.contentItem
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton

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
                ImButton {
                    text: qsTr("OK")
                    onClicked: storageRemovedDialog.close()
                }
            }
        }
    }

    Dialog {
        id: quitDialog
        modal: true
        parent: window.contentItem
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

    MsgPopup {
        id: confirmwritepopup
        continueButton: false
        yesButton: true
        noButton: true
        title: qsTr("Warning")
        modal: true
        onYes: {
            // Final validation check - device could have been removed while dialog was open
            if (!window.imageWriter.readyToWrite()) {
                msgpopup.title = qsTr("Storage device not available")
                msgpopup.text = qsTr("The selected storage device is no longer available.<br>Please select a different storage device.")
                msgpopup.open()
                return
            }
            
            langbarRect.visible = false
            writebutton.visible = false
            writebutton.enabled = false
            cancelwritebutton.enabled = true
            cancelwritebutton.visible = true
            cancelverifybutton.enabled = true
            progressText.text = qsTr("Preparing to write...");
            progressText.visible = true
            progressBar.visible = true
            progressBar.indeterminate = true
            progressBar.Material.accent = Style.progressBarTextColor
            // Wizard UI manages its own navigation; legacy button disables removed
            window.imageWriter.setVerifyEnabled(true)
            window.imageWriter.startWrite()
        }

        function askForConfirmation()
        {
            // Double-check that the selected drive is still valid before confirming
            if (!window.imageWriter.readyToWrite()) {
                msgpopup.title = qsTr("Storage device not available")
                msgpopup.text = qsTr("The selected storage device is no longer available.<br>Please select a different storage device.")
                msgpopup.open()
                return
            }
            
            text = qsTr("All existing data on '%1' will be erased.<br>Are you sure you want to continue?").arg(dstbutton.text)
            open()
        }

        onOpened: {
            forceActiveFocus()
        }
    }

    KeychainPermissionPopup {
        id: keychainpopup
        onAccepted: {
            window.imageWriter.keychainPermissionResponse(true)
        }
        onRejected: {
            window.imageWriter.keychainPermissionResponse(false)
        }
    }

    MsgPopup {
        id: updatepopup
        continueButton: false
        yesButton: true
        noButton: true
        property url url
        title: qsTr("Update available")
        text: qsTr("There is a newer version of Imager available.<br>Would you like to visit the website to download it?")
        onYes: {
            window.imageWriter.openUrl(url)
        }
    }

    OptionsPopup {
        id: optionspopup
        minimumWidth: 450
        minimumHeight: 400

        x: window.x + (window.width - width) / 2
        y: window.y + (window.height - height) / 2
    
        imageWriter: window.imageWriter

        onSaveSettingsSignal: (settings) => {
            window.imageWriter.setSavedCustomizationSettings(settings)
            usesavedsettingspopup.hasSavedSettings = true
        }
    }

    AdvancedOptionsPopup {
        id: advancedOptionsPopup
        imageWriter: window.imageWriter
    }

    UseSavedSettingsPopup {
        id: usesavedsettingspopup
        imageWriter: window.imageWriter

        onYes: {
            optionspopup.initialize()
            optionspopup.applySettings()
            confirmwritepopup.askForConfirmation()
        }
        onNo: {
            window.imageWriter.setImageCustomization("", "", "", "", "")
            confirmwritepopup.askForConfirmation()
        }
        onNoClearSettings: {
            hasSavedSettings = false
            optionspopup.clearCustomizationFields()
            window.imageWriter.clearSavedCustomizationSettings()
            confirmwritepopup.askForConfirmation()
        }
        onEditSettings: {
            optionspopup.show()
        }
        onCloseSettings: {
            optionspopup.close()
        }
    }

    MsgPopup {
        id: embeddedFinishedPopup
        continueButton: true
        rebootButton: true        
        title: qsTr("Write Successful")
        text: qsTr("<b>%1</b> has been written to <b>%2</b><br><br>You may now reboot").arg(wizardContainer.selectedOsName).arg(wizardContainer.selectedStorageName)

        onReboot: {
           window.imageWriter.reboot()
        }
        onClosed: {
            window.imageWriter.setDst("")
            window.selectedStorageName = ""
            window.resetWriteButton()
        }
    }

    /* Slots for signals imagewrite emits */
    function onDownloadProgress(now,total) {
        // Forward to wizard container
        wizardContainer.onDownloadProgress(now, total)
        
            if (isCacheVerifying) {
                isCacheVerifying = false
        }
    }

    function onVerifyProgress(now,total) {
        // Forward to wizard container
        wizardContainer.onVerifyProgress(now, total)
    }

    function onPreparationStatusUpdate(msg) {
        // Forward to wizard container
        wizardContainer.onPreparationStatusUpdate(msg)
    }

    function onOsListPrepared() {
        // OS list fetching is now handled by the wizard
    }

    function resetWriteButton() {
        // Reset handled by wizard interface now
    }

    function onError(msg) {
        errorDialog.titleText = qsTr("Error")
        errorDialog.message = msg
        errorDialog.open()
    }

    function onSuccess() {
        // Success is now handled by the wizard's done step
        if (imageWriter.isEmbeddedMode()) {
            embeddedFinishedPopup.open()
        }
    }

    function onFileSelected(file) {
        imageWriter.setSrc(file)
        window.selectedOsName = imageWriter.srcFileName()
        // File selection now handled by wizard
    }

    function onCancelled() {
        resetWriteButton()
    }

    function onFinalizing() {
        wizardContainer.onFinalizing()
    }

    function onNetworkInfo(msg) {
        if (imageWriter.isEmbeddedMode() && wizardContainer) {
            wizardContainer.networkInfoText = msg
        }
    }

    function onCacheVerificationStarted() {
        // Set cache verification state
        isCacheVerifying = true
    }

    function onCacheVerificationFinished() {
        // Clear cache verification state
        isCacheVerifying = false
    }

    Timer {
        /* Verify if default drive is in our list after 100 ms */
        id: setDefaultDest
        property string drive : ""
        interval: 100
        onTriggered: {
            for (var i = 0; i < window.driveListModel.rowCount(); i++)
            {
                /* FIXME: there should be a better way to iterate drivelist than
                   fetch data by numeric role number */
                if (window.driveListModel.data(window.driveListModel.index(i,0), 0x101) === drive) {
                    dstpopup.selectDstItem({
                                      device: drive,
                                      description: window.driveListModel.data(window.driveListModel.index(i,0), 0x102),
                                      size: window.driveListModel.data(window.driveListModel.index(i,0), 0x103),
                                      readonly: false
                                  })
                    break
                }
            }
        }
    }

    // Called from C++
    function fetchOSlist() {
        // OS list fetching is now handled by the wizard
    }

    // Called from C++ when selected device is removed
    function onSelectedDeviceRemoved() {
        // Clear storage selection since selected device no longer exists
        selectedStorageName = ""
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
