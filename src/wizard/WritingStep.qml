/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../qmlcomponents"

import RpiImager
import ImageOptions

WizardStepBase {
    id: root
    objectName: "writingStep"

    required property ImageWriter imageWriter
    required property var wizardContainer

    title: qsTr("Write image")
    subtitle: {
        if (root.isWriting) {
            return qsTr("Writing in progress — do not disconnect the storage device")
        } else if (root.isComplete) {
            return qsTr("Write complete")
        } else {
            return qsTr("Review your choices and write the image to the storage device")
        }
    }
    nextButtonText: {
        if (root.isWriting) {
            // Show specific cancel text based on write state
            if (imageWriter.writeState === ImageWriter.Verifying) {
                return qsTr("Skip verification")
            } else {
                return qsTr("Cancel write")
            }
        } else if (root.isComplete) {
            return CommonStrings.continueText
        } else {
            return qsTr("Write")
        }
    }
    nextButtonAccessibleDescription: {
        if (root.isWriting) {
            if (imageWriter.writeState === ImageWriter.Verifying) {
                return qsTr("Skip verification and finish the write process")
            } else {
                return qsTr("Cancel the write operation and return to the summary")
            }
        } else if (root.isComplete) {
            return qsTr("Continue to the completion screen")
        } else {
            return qsTr("Begin writing the image to the storage device. All existing data will be erased.")
        }
    }
    backButtonAccessibleDescription: qsTr("Return to previous customization step")
    nextButtonEnabled: root.isWriting || root.isComplete || imageWriter.readyToWrite()
    showBackButton: true

    property bool isWriting: false
    property bool isVerifying: false
    property bool cancelPending: false
    property bool isFinalising: false
    property bool isComplete: false
    property bool confirmOpen: false
    property string bottleneckStatus: ""
    property int writeThroughputKBps: 0
    readonly property bool anyCustomizationsApplied: (
        wizardContainer.customizationSupported && (
            wizardContainer.hostnameConfigured ||
            wizardContainer.localeConfigured ||
            wizardContainer.userConfigured ||
            wizardContainer.wifiConfigured ||
            wizardContainer.sshEnabled ||
            wizardContainer.piConnectEnabled ||
            wizardContainer.featUsbGadgetEnabled
        )
    )

    // Disable back while writing
    backButtonEnabled: !root.isWriting && !root.cancelPending && !root.isFinalising

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
            visible: !root.isWriting && !root.cancelPending && !root.isFinalising && !root.isComplete

            Text {
                id: summaryHeading
                text: qsTr("Summary")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
                Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
                activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
            }

            GridLayout {
                id: summaryGrid
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.spacingSmall

                Text {
                    id: deviceLabel
                    text: CommonStrings.device
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text + ": " + (wizardContainer.selectedDeviceName || CommonStrings.noDeviceSelected)
                    Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                    focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
                    activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                }

                MarqueeText {
                    id: deviceValue
                    text: wizardContainer.selectedDeviceName || CommonStrings.noDeviceSelected
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Accessible.ignored: true  // Read as part of the label
                }

                Text {
                    id: osLabel
                    text: qsTr("Operating system:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text + " " + (wizardContainer.selectedOsName || CommonStrings.noImageSelected)
                    Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                    focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
                    activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                }

                MarqueeText {
                    id: osValue
                    text: wizardContainer.selectedOsName || CommonStrings.noImageSelected
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Accessible.ignored: true  // Read as part of the label
                }

                Text {
                    id: storageLabel
                    text: CommonStrings.storage
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text + ": " + (wizardContainer.selectedStorageName || CommonStrings.noStorageSelected)
                    Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                    focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
                    activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                }

                MarqueeText {
                    id: storageValue
                    text: wizardContainer.selectedStorageName || CommonStrings.noStorageSelected
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Accessible.ignored: true  // Read as part of the label
                }
            }
        }

        // Customization summary (what will be written) - de-chromed
        ColumnLayout {
            id: customLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Style.sectionMaxWidth
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.spacingMedium
            visible: !root.isWriting && !root.cancelPending && !root.isFinalising && !root.isComplete && root.anyCustomizationsApplied

            Text {
                id: customizationsHeading
                text: qsTr("Customisations to apply:")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
                Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
                activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
            }

            ScrollView {
                id: customizationsScrollView
                Layout.fillWidth: true
                // Cap height so long lists become scrollable in default window size
                Layout.maximumHeight: Math.round(root.height * 0.4)
                clip: true
                activeFocusOnTab: true
                Accessible.role: Accessible.List
                Accessible.name: {
                    // Build a list of visible customizations to announce
                    var items = []
                    if (wizardContainer.hostnameConfigured) items.push(CommonStrings.hostnameConfigured)
                    if (wizardContainer.localeConfigured) items.push(CommonStrings.localeConfigured)
                    if (wizardContainer.userConfigured) items.push(CommonStrings.userAccountConfigured)
                    if (wizardContainer.wifiConfigured) items.push(CommonStrings.wifiConfigured)
                    if (wizardContainer.sshEnabled) items.push(CommonStrings.sshEnabled)
                    if (wizardContainer.piConnectEnabled) items.push(CommonStrings.piConnectEnabled)
                    if (wizardContainer.featUsbGadgetEnabled) items.push(CommonStrings.usbGadgetEnabled)
                    if (wizardContainer.ifI2cEnabled) items.push(CommonStrings.i2cEnabled)
                    if (wizardContainer.ifSpiEnabled) items.push(CommonStrings.spiEnabled)
                    if (wizardContainer.if1WireEnabled) items.push(CommonStrings.onewireEnabled)
                    if (wizardContainer.ifSerial !== "" && wizardContainer.ifSerial !== "Disabled") items.push(CommonStrings.serialConfigured)
                    
                    return items.length + " " + (items.length === 1 ? qsTr("customization") : qsTr("customizations")) + ": " + items.join(", ")
                }
                contentItem: Flickable {
                    id: customizationsFlickable
                    contentWidth: width
                    contentHeight: customizationsColumn.implicitHeight
                    interactive: contentHeight > height
                    clip: true
                    
                    Keys.onUpPressed: {
                        if (contentY > 0) {
                            contentY = Math.max(0, contentY - 20)
                        }
                    }
                    Keys.onDownPressed: {
                        var maxY = Math.max(0, contentHeight - height)
                        if (contentY < maxY) {
                            contentY = Math.min(maxY, contentY + 20)
                        }
                    }
                    Column {
                        id: customizationsColumn
                        width: parent.width
                        spacing: Style.spacingXSmall
                        Text { text: "• " + CommonStrings.hostnameConfigured;      font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.hostnameConfigured;         Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.localeConfigured;        font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.localeConfigured;           Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.userAccountConfigured;   font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.userConfigured;             Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.wifiConfigured;          font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.wifiConfigured;             Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.sshEnabled;              font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.sshEnabled;                 Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.piConnectEnabled;        font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.piConnectEnabled;           Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.usbGadgetEnabled;        font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.featUsbGadgetEnabled;       Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.i2cEnabled;              font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.ifI2cEnabled;               Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.spiEnabled;              font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.ifSpiEnabled;               Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.onewireEnabled;          font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.if1WireEnabled;             Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.serialConfigured;        font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.ifSerial !== "" && wizardContainer.ifSerial !== "Disabled"; Accessible.role: Accessible.ListItem; Accessible.name: text }
                    }
                }
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: Style.scrollBarWidth
                }
            }
        }

        // Progress section (de-chromed)
        ColumnLayout {
            id: progressLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Style.sectionMaxWidth
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.spacingMedium
            visible: root.isWriting || root.cancelPending || root.isFinalising || root.isComplete

            Text {
                id: progressText
                text: qsTr("Starting write process...")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                Accessible.role: Accessible.StatusBar
                Accessible.name: text
                Accessible.focusable: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
                focusPolicy: (root.imageWriter && root.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
                activeFocusOnTab: root.imageWriter ? root.imageWriter.isScreenReaderActive() : false
            }

            ProgressBar {
                id: progressBar
                Layout.fillWidth: true
                Layout.preferredHeight: Style.spacingLarge
                value: 0
                from: 0
                to: 100

                Material.accent: Style.progressBarVerifyForegroundColor
                Material.background: Style.progressBarBackgroundColor
                visible: (root.isWriting || root.isFinalising)
                Accessible.role: Accessible.ProgressBar
                Accessible.name: qsTr("Write progress")
                Accessible.description: progressText.text
            }
            
            // Bottleneck status indicator - shows what's limiting progress
            Text {
                id: bottleneckText
                text: {
                    if (root.bottleneckStatus !== "") {
                        if (root.writeThroughputKBps > 0) {
                            return root.bottleneckStatus + " (" + Math.round(root.writeThroughputKBps / 1024) + " MB/s)"
                        }
                        return root.bottleneckStatus
                    }
                    return ""
                }
                font.pixelSize: Style.fontSizeSmall
                font.family: Style.fontFamily
                color: Style.formLabelDisabledColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: root.isWriting && root.bottleneckStatus !== ""
            }
        }

        // Bottom spacer to vertically center progress section when writing/complete
        Item { Layout.fillHeight: true; visible: root.isWriting || root.isComplete }
    }
    ]

    // Handle next button clicks based on current state
    onNextClicked: {
        if (root.isWriting) {
            // If we're in verification phase, skip verification and let write complete successfully
            if (imageWriter.writeState === ImageWriter.Verifying) {
                imageWriter.skipCurrentVerification()
            } else {
                // Cancel the actual write operation
                root.cancelPending = true
                root.isVerifying = false
                root.isFinalising = true
                progressBar.value = 100
                progressText.text = qsTr("Finalising…")
                imageWriter.cancelWrite()
            }
        } else if (!root.isComplete) {
            // If warnings are disabled, skip the confirmation dialog
            if (wizardContainer.disableWarnings) {
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
        progressText.text = qsTr("Finalising...")
        progressBar.value = 100
    }

    // Confirmation dialog
    BaseDialog {
        id: confirmDialog
        imageWriter: root.imageWriter
        parent: root.Window.window ? root.Window.window.overlayRootItem : undefined
        anchors.centerIn: parent

        // Override height with maximum constraint to prevent excessive height, but allow natural sizing
        height: Math.min(400, contentLayout ? (contentLayout.implicitHeight + Style.cardPadding * 2) : 200)

        property bool allowAccept: false
        property int countdown: 2

        // Custom escape handling
        function escapePressed() {
            confirmDialog.close()
        }

        // Register focus groups when component is ready
        Component.onCompleted: {
            registerFocusGroup("warning", function(){ 
                // Only include warning texts when screen reader is active (otherwise they're not focusable)
                if (confirmDialog.imageWriter && confirmDialog.imageWriter.isScreenReaderActive()) {
                    return [warningText, permanentText]
                }
                return []
            }, 0)
            registerFocusGroup("buttons", function(){ 
                // Only include buttons when they're visible (after allowAccept becomes true)
                return confirmDialog.allowAccept ? [cancelButton, acceptBtn] : []
            }, 1)
        }

        onOpened: {
            allowAccept = false
            countdown = 2
            confirmDelay.start()
        }
        onClosed: {
            confirmDelay.stop()
            allowAccept = false
            countdown = 2
        }

        // Dialog content - now using BaseDialog's contentLayout
        Text {
            id: warningText
            text: qsTr("You are about to ERASE all data on: %1").arg(wizardContainer.selectedStorageName || qsTr("the storage device"))
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.Heading
            Accessible.name: text
            Accessible.ignored: false
            Accessible.focusable: confirmDialog.imageWriter ? confirmDialog.imageWriter.isScreenReaderActive() : false
            focusPolicy: (confirmDialog.imageWriter && confirmDialog.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
            activeFocusOnTab: confirmDialog.imageWriter ? confirmDialog.imageWriter.isScreenReaderActive() : false
        }

        Text {
            id: permanentText
            text: qsTr("This action is PERMANENT and CANNOT be undone.")
            font.pixelSize: Style.fontSizeFormLabel
            font.family: Style.fontFamilyBold
            color: Style.formLabelColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
            Accessible.ignored: false
            Accessible.focusable: confirmDialog.imageWriter ? confirmDialog.imageWriter.isScreenReaderActive() : false
            focusPolicy: (confirmDialog.imageWriter && confirmDialog.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
            activeFocusOnTab: confirmDialog.imageWriter ? confirmDialog.imageWriter.isScreenReaderActive() : false
        }

        Text {
            id: waitText
            text: qsTr("Please wait... %1").arg(confirmDialog.countdown)
            font.pixelSize: Style.fontSizeFormLabel
            font.family: Style.fontFamily
            color: Style.textMetadataColor
            horizontalAlignment: Text.AlignRight
            Layout.fillWidth: true
            Layout.topMargin: Style.spacingSmall
            visible: !confirmDialog.allowAccept
        }

        RowLayout {
            id: confirmButtonRow
            Layout.fillWidth: true
            Layout.topMargin: Style.spacingSmall
            spacing: Style.spacingMedium
            visible: confirmDialog.allowAccept
            Item { Layout.fillWidth: true }

            ImButton {
                id: cancelButton
                text: CommonStrings.cancel
                accessibleDescription: qsTr("Cancel and return to the write summary without erasing the storage device")
                activeFocusOnTab: true
                onClicked: confirmDialog.close()
            }

            ImButtonRed {
                id: acceptBtn
                text: confirmDialog.allowAccept ? qsTr("I understand, erase and write") : qsTr("Please wait...")
                accessibleDescription: qsTr("Confirm erasure and begin writing the image to the storage device")
                enabled: confirmDialog.allowAccept
                activeFocusOnTab: true
                onClicked: {
                    confirmDialog.close()
                    beginWriteDelay.start()
                }
            }
        }

        // Bottom spacer to balance the dialog's internal top padding
        Item { Layout.preferredHeight: Style.cardPadding }
    }

    // Delay accept for 2 seconds - moved outside dialog content
    Timer {
        id: confirmDelay
        interval: 1000
        running: false
        repeat: true
        onTriggered: {
            confirmDialog.countdown--
            if (confirmDialog.countdown <= 0) {
                confirmDelay.stop()
                confirmDialog.allowAccept = true
                // Rebuild focus order now that buttons are visible
                confirmDialog.rebuildFocusOrder()
            }
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
            root.bottleneckStatus = ""
            root.writeThroughputKBps = 0
            progressText.text = qsTr("Starting write process...")
            progressBar.value = 0
            Qt.callLater(function(){ imageWriter.startWrite() })
        }
    }

    function onDownloadProgress(now, total) {
        // Download progress is tracked for performance stats but not shown in UI
        // (the write progress is more accurate as it reflects actual data written to disk)
    }

    function onWriteProgress(now, total) {
        if (root.isWriting) {
            var progress = total > 0 ? (now / total) * 100 : 0
            progressBar.value = progress
            progressText.text = qsTr("Writing... %1%").arg(Math.round(progress))
        }
    }

    function onVerifyProgress(now, total) {
        if (root.isWriting) {
            root.isVerifying = true
            root.bottleneckStatus = ""  // Clear write bottleneck during verification
            var progress = total > 0 ? (now / total) * 100 : 0
            progressBar.value = progress
            progressText.text = qsTr("Verifying... %1%").arg(Math.round(progress))
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
            root.cancelPending = false
            root.isFinalising = false
            root.isComplete = true
            progressText.text = qsTr("Write completed successfully!")

            // Automatically advance to the done screen
            wizardContainer.nextStep()
        }
        function onError(msg) {
            root.isWriting = false
            wizardContainer.isWriting = false
            root.cancelPending = false
            root.isFinalising = false
            progressText.text = qsTr("Write failed: %1").arg(msg)
        }

        function onFinalizing() {
            if (root.isWriting || root.cancelPending) {
                root.isVerifying = false
                root.isFinalising = true
                progressText.text = qsTr("Finalising…")
                progressBar.value = 100
            }
        }
        
        function onBottleneckStatusChanged(status, throughputKBps) {
            root.bottleneckStatus = status
            root.writeThroughputKBps = throughputKBps
        }
    }
    
    // Focus management - rebuild when visibility changes between phases
    onIsWritingChanged: rebuildFocusOrder()
    onIsCompleteChanged: rebuildFocusOrder()
    onAnyCustomizationsAppliedChanged: rebuildFocusOrder()
    
    Component.onCompleted: {
        // Register summary section as first focus group
        registerFocusGroup("summary", function() {
            var items = []
            if (summaryLayout.visible) {
                // Only include text labels when screen reader is active
                if (root.imageWriter && root.imageWriter.isScreenReaderActive()) {
                    items.push(summaryHeading)
                    items.push(deviceLabel)
                    items.push(osLabel)
                    items.push(storageLabel)
                }
            }
            return items
        }, 0)
        
        // Register customizations section as second focus group
        registerFocusGroup("customizations", function() {
            var items = []
            if (customLayout.visible) {
                // Only include heading when screen reader is active; always include scroll view
                if (root.imageWriter && root.imageWriter.isScreenReaderActive()) {
                    items.push(customizationsHeading)
                }
                items.push(customizationsScrollView)
            }
            return items
        }, 1)
        
        // Register progress section (when writing/complete)
        registerFocusGroup("progress", function() {
            var items = []
            if (progressLayout.visible) {
                // Only include progress text when screen reader is active
                if (root.imageWriter && root.imageWriter.isScreenReaderActive()) {
                    items.push(progressText)
                }
                // Always include progress bar when visible (during writing)
                if (progressBar.visible) {
                    items.push(progressBar)
                }
            }
            return items
        }, 0)
        
        // Let WizardStepBase handle initial focus (title first)
        // Ensure focus order is built when component completes
        Qt.callLater(function() {
            rebuildFocusOrder()
        })
    }
}
