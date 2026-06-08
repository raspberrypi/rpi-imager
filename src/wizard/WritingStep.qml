/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import "../qmlcomponents"

import RpiImager
import ImageOptions

WizardStepBase {
    id: root
    objectName: "writingStep"

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
            if (ImageWriterSingleton.writeState === ImageWriterSingleton.Verifying) {
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
            if (ImageWriterSingleton.writeState === ImageWriterSingleton.Verifying) {
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
    nextButtonEnabled: root.isWriting || root.isComplete || (!beginWriteDelay.running && ImageWriterSingleton.readyToWrite())
    showBackButton: true

    readonly property bool isWriting: {
        var s = ImageWriterSingleton.writeState
        return s === ImageWriterSingleton.Preparing || s === ImageWriterSingleton.Writing ||
               s === ImageWriterSingleton.Verifying || s === ImageWriterSingleton.Finalizing ||
               s === ImageWriterSingleton.Cancelling
    }
    readonly property bool isVerifying: ImageWriterSingleton.writeState === ImageWriterSingleton.Verifying
    readonly property bool isCancelling: ImageWriterSingleton.writeState === ImageWriterSingleton.Cancelling
    readonly property bool isFinalising: ImageWriterSingleton.writeState === ImageWriterSingleton.Finalizing
    readonly property bool isComplete: ImageWriterSingleton.writeState === ImageWriterSingleton.Succeeded
    property string bottleneckStatus: ""
    property int writeThroughputKBps: 0
    property string operationWarning: ""  // Non-fatal warning message (e.g., sync fallback)
    property bool isIndeterminateProgress: false  // True when we can't determine accurate progress (e.g., gz files >4GB)
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
    backButtonEnabled: !root.isWriting

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
            visible: !root.isWriting && !root.isComplete

            FocusableHeading {
                id: summaryHeading
                text: qsTr("Summary")
                font.pointSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
            }

            GridLayout {
                id: summaryGrid
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.spacingSmall

                FocusableText {
                    id: deviceLabel
                    text: CommonStrings.device
                    font.pointSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.name: text + ": " + (root.wizardContainer.selectedDeviceName || CommonStrings.noDeviceSelected)
                }

                MarqueeText {
                    id: deviceValue
                    text: root.wizardContainer.selectedDeviceName || CommonStrings.noDeviceSelected
                    font.pointSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Accessible.ignored: true  // Read as part of the label
                }

                FocusableText {
                    id: osLabel
                    text: qsTr("Operating system:")
                    font.pointSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.name: text + " " + (root.wizardContainer.selectedOsName || CommonStrings.noImageSelected)
                }

                MarqueeText {
                    id: osValue
                    text: root.wizardContainer.selectedOsName || CommonStrings.noImageSelected
                    font.pointSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Accessible.ignored: true  // Read as part of the label
                }

                FocusableText {
                    id: storageLabel
                    text: CommonStrings.storage
                    font.pointSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.name: text + ": " + (root.wizardContainer.selectedStorageName || CommonStrings.noStorageSelected)
                }

                MarqueeText {
                    id: storageValue
                    text: root.wizardContainer.selectedStorageName || CommonStrings.noStorageSelected
                    font.pointSize: Style.fontSizeDescription
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
            visible: !root.isWriting && !root.isComplete && root.anyCustomizationsApplied

            FocusableHeading {
                id: customizationsHeading
                text: qsTr("Customisations to apply:")
                font.pointSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
            }

            ScrollView {
                id: customizationsScrollView
                Layout.fillWidth: true
                // Cap height so long lists become scrollable in default window size
                Layout.maximumHeight: Math.round(root.height * 0.4)
                clip: true
                activeFocusOnTab: true
                focusPolicy: Qt.TabFocus
                Accessible.role: Accessible.List
                Accessible.name: {
                    // Build a list of visible customizations to announce
                    var items = []
                    if (root.wizardContainer.hostnameConfigured) items.push(CommonStrings.hostnameConfigured)
                    if (root.wizardContainer.localeConfigured) items.push(CommonStrings.localeConfigured)
                    if (root.wizardContainer.userConfigured) items.push(CommonStrings.userAccountConfigured)
                    if (root.wizardContainer.wifiConfigured) items.push(CommonStrings.wifiConfigured)
                    if (root.wizardContainer.sshEnabled) items.push(CommonStrings.sshEnabled)
                    if (root.wizardContainer.piConnectEnabled) items.push(CommonStrings.piConnectEnabled)
                    if (root.wizardContainer.featUsbGadgetEnabled) items.push(CommonStrings.usbGadgetEnabled)
                    if (root.wizardContainer.ifI2cEnabled) items.push(CommonStrings.i2cEnabled)
                    if (root.wizardContainer.ifSpiEnabled) items.push(CommonStrings.spiEnabled)
                    if (root.wizardContainer.if1WireEnabled) items.push(CommonStrings.onewireEnabled)
                    if (root.wizardContainer.ifSerial !== "" && root.wizardContainer.ifSerial !== "Disabled") items.push(CommonStrings.serialConfigured)
                    
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
                        Text { text: "• " + CommonStrings.hostnameConfigured;      font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.hostnameConfigured;         Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.localeConfigured;        font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.localeConfigured;           Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.userAccountConfigured;   font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.userConfigured;             Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.wifiConfigured;          font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.wifiConfigured;             Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.sshEnabled;              font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.sshEnabled;                 Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.piConnectEnabled;        font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.piConnectEnabled;           Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.usbGadgetEnabled;        font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.featUsbGadgetEnabled;       Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.i2cEnabled;              font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.ifI2cEnabled;               Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.spiEnabled;              font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.ifSpiEnabled;               Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.onewireEnabled;          font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.if1WireEnabled;             Accessible.role: Accessible.ListItem; Accessible.name: text }
                        Text { text: "• " + CommonStrings.serialConfigured;        font.pointSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: root.wizardContainer.ifSerial !== "" && root.wizardContainer.ifSerial !== "Disabled"; Accessible.role: Accessible.ListItem; Accessible.name: text }
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
            visible: root.isWriting || root.isComplete

            FocusableText {
                id: progressText
                text: qsTr("Starting write process...")
                font.pointSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                Accessible.role: Accessible.StatusBar
            }

            ProgressBar {
                id: progressBar
                Layout.fillWidth: true
                Layout.preferredHeight: Style.spacingLarge
                value: 0
                from: 0
                to: 100
                indeterminate: root.isIndeterminateProgress && !root.isVerifying && !root.isFinalising
                               && !PlatformHelper.prefersReducedMotion

                Material.accent: Style.progressBarVerifyForegroundColor
                Material.background: Style.progressBarBackgroundColor
                visible: root.isWriting
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
                font.pointSize: Style.fontSizeSmall
                font.family: Style.fontFamily
                color: Style.formLabelDisabledColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: root.isWriting && root.bottleneckStatus !== ""
            }
            
            // Operation warning (e.g., sync fallback due to slow device)
            Text {
                id: operationWarningText
                text: "⚠ " + root.operationWarning
                font.pointSize: Style.fontSizeSmall
                font.family: Style.fontFamily
                color: "#FFA500"  // Orange/amber for warning
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: root.isWriting && root.operationWarning !== ""
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
            if (ImageWriterSingleton.writeState === ImageWriterSingleton.Verifying) {
                ImageWriterSingleton.skipCurrentVerification()
            } else {
                // Cancel the actual write operation
                progressBar.value = 100
                progressText.text = qsTr("Finalising…")
                ImageWriterSingleton.cancelWrite()
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
        progressText.text = qsTr("Finalising...")
        progressBar.value = 100
    }

    // Confirmation dialog
    BaseDialog {
        id: confirmDialog
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
                if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
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
            // If a screen reader is active, bypass the timer - screen reader users
            // need time to hear the content, not wait for a visual countdown
            if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
                allowAccept = true
                countdown = 0
                rebuildFocusOrder()
                focusInitialItem()
            } else {
                allowAccept = false
                countdown = 2
                confirmDelay.start()
            }
        }
        onClosed: {
            confirmDelay.stop()
            allowAccept = false
            countdown = 2
        }

        // Dialog content - now using BaseDialog's contentLayout
        FocusableHeading {
            id: warningText
            text: qsTr("You are about to ERASE all data on: %1").arg(root.wizardContainer.selectedStorageName || qsTr("the storage device"))
            font.pointSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.ignored: false
        }

        FocusableText {
            id: permanentText
            text: qsTr("This action is PERMANENT and CANNOT be undone.")
            font.pointSize: Style.fontSizeFormLabel
            font.family: Style.fontFamilyBold
            color: Style.formLabelColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.ignored: false
        }

        Text {
            id: waitText
            text: qsTr("Please wait... %1").arg(confirmDialog.countdown)
            font.pointSize: Style.fontSizeFormLabel
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
                // Rebuild focus order now that buttons are visible, then move
                // focus onto Cancel so Tab/Enter work without a mouse click.
                confirmDialog.rebuildFocusOrder()
                confirmDialog.focusInitialItem()
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
            root.bottleneckStatus = ""
            root.writeThroughputKBps = 0
            root.operationWarning = ""
            // Check if extract size is known upfront (e.g., gz files can't reliably store sizes >4GB)
            root.isIndeterminateProgress = !ImageWriterSingleton.isExtractSizeKnown()
            progressText.text = qsTr("Starting write process...")
            progressBar.value = 0
            ImageWriterSingleton.startWrite()
        }
    }

    function onDownloadProgress(now, total) {
        // Download progress is tracked for performance stats but not shown in UI
        // (the write progress is more accurate as it reflects actual data written to disk)
    }

    function onWriteProgress(now, total) {
        if (root.isWriting) {
            if (root.isIndeterminateProgress) {
                // Show indeterminate progress with bytes written (in human-readable format)
                var bytesWrittenMB = Math.round(now / (1024 * 1024))
                progressText.text = qsTr("Writing... %1 MB written").arg(bytesWrittenMB)
            } else {
                var progress = total > 0 ? (now / total) * 100 : 0
                progressBar.value = progress
                progressText.text = qsTr("Writing... %1%").arg(Math.round(progress))
            }
        }
    }

    function onVerifyProgress(now, total) {
        if (root.isWriting) {
            root.operationWarning = ""  // Clear write warnings during verification
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
        target: ImageWriterSingleton
        function onSuccess() {
            progressText.text = qsTr("Write completed successfully!")

            // Automatically advance to the done screen
            root.wizardContainer.nextStep()
        }
        function onError(msg) {
            progressText.text = qsTr("Write failed: %1").arg(msg)
        }

        function onFinalizing() {
            if (root.isWriting) {
                progressText.text = qsTr("Finalising…")
                progressBar.value = 100
            }
        }
        
        function onBottleneckStatusChanged(status, throughputKBps) {
            root.bottleneckStatus = status
            root.writeThroughputKBps = throughputKBps
        }
        
        function onOperationWarning(message) {
            root.operationWarning = message
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
                if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
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
                if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
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
                if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
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
