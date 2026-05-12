/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import "qmlcomponents"
import "wizard"
import "wizard/dialogs"

import RpiImager

ApplicationWindow {
    id: window
    visible: true

    // Whether to show the landing Language Selection step (set from C++)
    property bool showLanguageSelection: false
    // Wizard manages drive list and selection state
    property bool forceQuit: false
    // Expose overlay root to child components for dialog parenting
    readonly property alias overlayRootItem: overlayRoot

    width: ImageWriterSingleton.isEmbeddedMode() ? -1 : Style.scaled(680)
    height: ImageWriterSingleton.isEmbeddedMode() ? -1 : Style.scaled(450)
    minimumWidth: ImageWriterSingleton.isEmbeddedMode() ? -1 : Style.scaled(680)
    minimumHeight: ImageWriterSingleton.isEmbeddedMode() ? -1 : Style.scaled(420)

    // Track custom repo host for title display
    property string customRepoHost: ImageWriterSingleton.customRepoHost()
    
    // Track offline state for title display (derived from whether OS list data is available)
    property bool isOffline: ImageWriterSingleton.isOsListUnavailable
    
    title: {
        var baseTitle = qsTr("Raspberry Pi Imager %1").arg(ImageWriterSingleton.constantVersion())
        if (isOffline) {
            baseTitle += " — " + qsTr("Offline")
        }

        if (customRepoHost.length > 0) {
            baseTitle += " — " + qsTr("Using data from %1").arg(customRepoHost)
        }

        return baseTitle
    }

    Component.onCompleted: {
        // Set the main window for modal file dialogs
        ImageWriterSingleton.setMainWindow(window)
        // Initial privileged-helper-state evaluation: the C++ constructor
        // probes state before QML is wired, so a startup with the helper
        // already disabled produces no transition for Connections to fire
        // on. Evaluate once here so the dialog opens at launch when needed.
        window._evaluatePrivilegedHelperState()
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

    // Keyboard shortcut to export performance data (Ctrl+Shift+P)
    Shortcut {
        sequence: "Ctrl+Shift+P"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (ImageWriterSingleton.hasPerformanceData()) {
                console.log("Exporting performance data...")
                ImageWriterSingleton.exportPerformanceData()
            } else {
                console.log("No performance data available to export")
            }
        }
    }
    
    // Secret keyboard shortcut to open debug options (Cmd+Option+S on macOS, Ctrl+Alt+S on others)
    Shortcut {
        sequence: "Ctrl+Alt+S"
        context: Qt.ApplicationShortcut
        onActivated: {
            console.log("Opening debug options dialog...")
            debugOptionsLoader.active = true
            debugOptionsLoader.item.initialize()
            debugOptionsLoader.item.open()
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
            overlayRootRef: overlayRoot
            // Show Language step if C++ requested it
            showLanguageSelection: window.showLanguageSelection

            onWizardCompleted: {
                // Reset to start of wizard or close application
                wizardContainer.currentStep = 0;
            }

            onAppOptionsRequested: {
                appOptionsLoader.active = true
                appOptionsLoader.item.initialize()
                appOptionsLoader.item.open()
            }

            onUpdatePopupRequested: function(updateUrl, version) {
                if (!window.updatePopupShown) {
                    window.updatePopupShown = true
                    updatepopup.url = updateUrl
                    updatepopup.version = version
                    updatepopup.open()
                }
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
            registerFocusGroup("content", function(){ 
                // Only include text elements when screen reader is active (otherwise they're not focusable)
                if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
                    return [errorTitle, errorMessage]
                }
                return []
            }, 0)
            registerFocusGroup("buttons", function(){ 
                return [errorContinueButton] 
            }, 1)
        }

        // Dialog content
        FocusableHeading {
            id: errorTitle
            text: errorDialog.titleText
            font.pointSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        FocusableText {
            id: errorMessage
            text: errorDialog.message
            textFormat: Text.StyledText
            wrapMode: Text.WordWrap
            font.pointSize: Style.fontSizeDescription
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            Layout.fillWidth: true
            Accessible.name: text.replace(/<[^>]+>/g, '')  // Strip HTML tags for accessibility
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
            registerFocusGroup("content", function(){ 
                // Only include text elements when screen reader is active (otherwise they're not focusable)
                if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
                    return [storageRemovedTitle, storageRemovedMessage]
                }
                return []
            }, 0)
            registerFocusGroup("buttons", function(){ 
                return [storageOkButton] 
            }, 1)
        }

        // Dialog content
        FocusableHeading {
            id: storageRemovedTitle
            text: qsTr("Storage device removed")
            font.pointSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        FocusableText {
            id: storageRemovedMessage
            text: qsTr("The storage device was removed while writing, so the operation was cancelled. Please reinsert the device or select a different one to continue.")
            wrapMode: Text.WordWrap
            font.pointSize: Style.fontSizeDescription
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
            registerFocusGroup("content", function(){ 
                // Only include text elements when screen reader is active (otherwise they're not focusable)
                if (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) {
                    return [quitTitle, quitMessage]
                }
                return []
            }, 0)
            registerFocusGroup("buttons", function(){ 
                return [quitNoButton, quitYesButton] 
            }, 1)
        }

        // Dialog content
        FocusableHeading {
            id: quitTitle
            text: qsTr("Are you sure you want to quit?")
            font.pointSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelColor
            Layout.fillWidth: true
        }

        FocusableText {
            id: quitMessage
            text: qsTr("Raspberry Pi Imager is still busy. Are you sure you want to quit?")
            font.pointSize: Style.fontSizeDescription
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
            ImageWriterSingleton.keychainPermissionResponse(true);
        }
        onRejected: {
            ImageWriterSingleton.keychainPermissionResponse(false);
        }
    }

    // Privileged-helper UX (macOS SMAppService). Shown automatically when
    // the helper isn't ready: either we need to register it for the first
    // time, or the user has to flip the toggle in System Settings.
    PrivilegedHelperDialog {
        id: privilegedHelperDialog
        parent: overlayRoot
        helperState: ImageWriterSingleton ? ImageWriterSingleton.privilegedHelperState
                                          : ImageWriter.Unknown
        onAccepted: {
            // User confirmed the explainer for first-time install.
            // The first device-open call from the imager pipeline will
            // call SMAppService.register() (via XpcFileOperations) and
            // macOS will surface its native prompt. We could trigger it
            // proactively here but that creates a chicken-and-egg with
            // queryHelperStatus' caching; deferring to the next open
            // keeps the flow uniform with returning users.
        }
    }
    function _evaluatePrivilegedHelperState() {
        if (!ImageWriterSingleton) return
        const s = ImageWriterSingleton.privilegedHelperState
        if (s === ImageWriter.NeedsInstall ||
            s === ImageWriter.NeedsApproval) {
            if (!privilegedHelperDialog.opened) privilegedHelperDialog.open()
        } else if (privilegedHelperDialog.opened) {
            privilegedHelperDialog.close()
        }
    }
    Connections {
        target: ImageWriterSingleton
        function onPrivilegedHelperStateChanged() {
            window._evaluatePrivilegedHelperState()
        }
        function onError() {
            // After any open/write failure, re-probe the helper state.
            // If the user disabled it from Settings mid-session, this is
            // how we discover that and surface the right dialog instead
            // of a bare "Failed to open disk" message.
            ImageWriterSingleton.refreshPrivilegedHelperState()
        }
    }
    Timer {
        // While the user is parked on the "open System Settings" dialog,
        // re-probe periodically so that flipping the toggle closes the
        // dialog without further interaction. Cheap (one SMAppService.status
        // read) so 2 s polling is fine.
        interval: 2000
        repeat: true
        running: privilegedHelperDialog.opened
                  && ImageWriterSingleton
                  && ImageWriterSingleton.privilegedHelperState === ImageWriter.NeedsApproval
        onTriggered: {
            ImageWriterSingleton.refreshPrivilegedHelperState()
        }
    }

    // Track whether update popup has been shown this session
    property bool updatePopupShown: false

    UpdateAvailableDialog {
        id: updatepopup
        // parent can be set to overlayRoot if needed for centering above
        parent: overlayRoot
        onAccepted: {}
        onRejected: {}
    }

    // Permission warning dialog for when not running with elevated privileges
    BaseDialog {
        id: permissionWarningDialog
        parent: overlayRoot
        anchors.centerIn: parent
        closePolicy: Popup.NoAutoClose  // Prevent closing with escape or clicking outside
        
        property string warningMessage: ""
        
        function showWarning(message) {
            warningMessage = message
            open()
        }
        
        // Custom escape handling - exit the application
        function escapePressed() {
            Qt.quit()
        }
        
        // Register focus groups when component is ready
        Component.onCompleted: {
            registerFocusGroup("heading", function(){ 
                return [headingText] 
            }, 0)
            registerFocusGroup("message", function(){ 
                return [messageText] 
            }, 1)
            registerFocusGroup("buttons", function(){ 
                return [installAuthButton, exitButton]
            }, 2)
        }
        
        // Dialog content
        FocusableHeading {
            id: headingText
            text: qsTr("Insufficient Permissions")
            font.pointSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelErrorColor
            Layout.fillWidth: true
            Accessible.description: text
        }
        
        FocusableText {
            id: messageText
            text: permissionWarningDialog.warningMessage
            font.pointSize: Style.fontSizeDescription
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.description: qsTr("Error message explaining why elevated privileges are required")
            Accessible.ignored: false
        }
        
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item {
                Layout.fillWidth: true
            }
            
            // Install Authorization button - only visible for elevatable bundles (e.g., AppImage)
            ImButton {
                id: installAuthButton
                text: qsTr("Install Authorization")
                accessibleDescription: qsTr("Install system authorization to allow Raspberry Pi Imager to run with elevated privileges")
                activeFocusOnTab: true
                visible: ImageWriterSingleton && ImageWriterSingleton.isElevatableBundle()
                // Make button wide enough to fit the text, with sensible bounds
                Layout.minimumWidth: Style.buttonWidthMinimum
                Layout.maximumWidth: Style.buttonWidthMinimum * 2  // Cap at 2x to handle long translations
                implicitWidth: Math.max(Style.buttonWidthMinimum, implicitContentWidth + leftPadding + rightPadding)
                onClicked: {
                    if (ImageWriterSingleton.installElevationPolicy()) {
                        // Policy installed successfully - restart with elevated privileges
                        ImageWriterSingleton.restartWithElevatedPrivileges()
                    }
                }
            }
            
            ImButtonRed {
                id: exitButton
                text: qsTr("Exit")
                accessibleDescription: qsTr("Exit Raspberry Pi Imager - you must restart with elevated privileges to write images")
                activeFocusOnTab: true
                onClicked: Qt.quit()
            }
        }
    }

    // Lazily constructed: each of these dialogs is ~600 lines of QML and is only
    // reachable on user action (the "App Options" button / a debug shortcut), so we
    // keep them off the startup path. The Loader is inactive until first use; once
    // realized it persists, so subsequent opens reuse the same instance.
    Loader {
        id: appOptionsLoader
        active: false
        sourceComponent: AppOptionsDialog {
            parent: overlayRoot
            wizardContainer: wizardContainer
        }
    }

    Loader {
        id: debugOptionsLoader
        active: false
        sourceComponent: DebugOptionsDialog {
            parent: overlayRoot
            wizardContainer: wizardContainer
        }
    }

    // Removed embeddedFinishedPopup; handled by Wizard Done step

    // QML fallback save dialog for performance data export
    ImSaveFileDialog {
        id: performanceSaveDialog
        parent: overlayRoot
        anchors.centerIn: parent
        dialogTitle: qsTr("Save Performance Data")
        nameFilters: [qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        
        onAccepted: {
            var filePath = String(selectedFile)
            // Strip file:// prefix for the C++ call
            if (filePath.indexOf("file://") === 0) {
                filePath = filePath.substring(7)
            }
            if (filePath.length > 0) {
                console.log("Saving performance data to:", filePath)
                ImageWriterSingleton.exportPerformanceDataToFile(filePath)
            }
        }
    }

    // Handle signal from C++ when native save dialog isn't available
    Connections {
        target: ImageWriterSingleton
        function onPerformanceSaveDialogNeeded(suggestedFilename, initialDir) {
            console.log("Native save dialog not available, using QML fallback")
            performanceSaveDialog.suggestedFilename = suggestedFilename
            var folderUrl = (Qt.platform.os === "windows") ? ("file:///" + initialDir) : ("file://" + initialDir)
            performanceSaveDialog.currentFolder = folderUrl
            performanceSaveDialog.folder = folderUrl
            performanceSaveDialog.open()
        }
        
        // Update title when custom repository changes
        function onCustomRepoChanged() {
            window.customRepoHost = ImageWriterSingleton.customRepoHost()
        }
        
        // Update title when repo host changes after redirect
        // This ensures "Using data from X" shows the final URL host after redirects
        function onCustomRepoHostChanged() {
            window.customRepoHost = ImageWriterSingleton.customRepoHost()
        }
    }

    /* Slots for signals imagewrite emits */
    function onDownloadProgress(now, total) {
        // Forward to wizard container
        wizardContainer.onDownloadProgress(now, total);
    }

    function onWriteProgress(now, total) {
        // Forward to wizard container
        wizardContainer.onWriteProgress(now, total);
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

    function onCancelled() {
        // Forward to wizard container to handle write cancellation
        if (wizardContainer) {
            wizardContainer.onWriteCancelled();
        }
    }

    function onNetworkInfo(msg) {
        if (ImageWriterSingleton.isEmbeddedMode() && wizardContainer) {
            wizardContainer.networkInfoText = msg;
        }
    }

    // Called from C++ when selected device is removed
    function onSelectedDeviceRemoved() {
        if (wizardContainer) {
            wizardContainer.selectedStorageName = "";
        }
        ImageWriterSingleton.setDst("");

        // If we are past storage selection, navigate back there
        if (wizardContainer && wizardContainer.currentStep > wizardContainer.stepStorageSelection) {
            wizardContainer.jumpToStep(wizardContainer.stepStorageSelection);
            // Inform the user with the dedicated modern dialog (only if we navigated back)
            storageRemovedDialog.open();
        }
        // If we're already on storage selection screen, don't show dialog - user can see the device disappeared
    }

    // Called from C++ when a write was cancelled because the storage device was removed
    function onWriteCancelledDueToDeviceRemoval() {
        if (wizardContainer) {
            wizardContainer.selectedStorageName = "";
        }
        // Clear backend dst reference
        ImageWriterSingleton.setDst("");
        // Navigate back to storage selection for safety
        if (wizardContainer)
            wizardContainer.jumpToStep(wizardContainer.stepStorageSelection);
        // Show dedicated dialog
        storageRemovedDialog.open();
    }

    function onKeychainPermissionRequested() {
        // If warnings are disabled, automatically grant permission without showing dialog
        if (wizardContainer.disableWarnings) {
            ImageWriterSingleton.keychainPermissionResponse(true);
        } else {
            keychainpopup.askForPermission();
        }
    }
    
    function onPermissionWarning(message) {
        permissionWarningDialog.showWarning(message);
    }

}
