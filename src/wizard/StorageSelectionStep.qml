/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("Select your storage device")
    showNextButton: true
    nextButtonEnabled: wizardContainer && wizardContainer.selectedStorageName && wizardContainer.selectedStorageName.length > 0  // Disabled until user selects a storage device
    
    // Forward the nextClicked signal as next() function
    // Only auto-advance if no system drive confirmation dialog is needed
    function next() {
        root.nextClicked()
    }
    
    // Conditional next function for keyboard auto-advance
    // Only auto-advance for non-system drives
    function conditionalNext() {
        // Check if current selection is a system drive
        if (dstlist.currentIndex >= 0) {
            var currentItem = dstlist.itemAtIndex(dstlist.currentIndex)
            if (currentItem && currentItem.isSystem) {
                // Don't auto-advance for system drives - let the confirmation dialog handle it
                return
            }
            
            // Fallback: check model directly when itemAtIndex returns null
            // (e.g., when using screen readers on Windows)
            if (!currentItem) {
                var model = root.imageWriter.getDriveList()
                if (model && dstlist.currentIndex < model.rowCount()) {
                    var modelIndex = model.index(dstlist.currentIndex, 0)
                    if (modelIndex && modelIndex.valid) {
                        var isSystemRole = 0x107
                        var isSystem = model.data(modelIndex, isSystemRole)
                        if (isSystem) {
                            // Don't auto-advance for system drives
                            return
                        }
                    }
                }
            }
        }
        // Safe to auto-advance for non-system drives
        root.nextClicked()
    }
    
    property alias dstlist: dstlist
    property string selectedDeviceName: ""
    property bool hasValidStorageOptions: false
    property bool hasAnyDevices: false
    property bool hasOnlyReadOnlyDevices: false
    property string enumerationErrorMessage: ""
    
    Component.onCompleted: {
        // Register the ListView for keyboard navigation
        root.registerFocusGroup("storage_list", function(){
            return [dstlist, filterSystemDrives]
        }, 0)
        
        // Set the initial focus item to the ListView
        // Initial focus will automatically go to title, then first control (handled by WizardStepBase)
        
        // Initialize hasValidStorageOptions
        root.updateStorageStatus()
    }
    
    // Watch for device removal - when selectedStorageName becomes empty, clear the currentIndex
    // This ensures that when a device is re-inserted, it won't appear highlighted but not actually selected
    Connections {
        target: wizardContainer
        function onSelectedStorageNameChanged() {
            if (!wizardContainer.selectedStorageName || wizardContainer.selectedStorageName.length === 0) {
                // Device was removed - clear the visual selection and ensure next button is disabled
                dstlist.currentIndex = -1
                root.selectedDeviceName = ""
                root.nextButtonEnabled = false
            }
        }
    }
    
    // Watch for drive enumeration errors from the model
    // This handles cases where the OS can't enumerate drives (permissions, driver issues, etc.)
    Connections {
        target: root.imageWriter ? root.imageWriter.getDriveList() : null
        function onEnumerationError(errorMessage) {
            root.enumerationErrorMessage = errorMessage || ""
            if (errorMessage) {
                console.warn("Drive enumeration error:", errorMessage)
            }
        }
    }
    
    // Removed drive list polling calls; backend handles device updates
    
    // Content
    content: [
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // Error banner for drive enumeration failures
        Rectangle {
            id: enumerationErrorBanner
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? errorBannerContent.implicitHeight + Style.spacingMedium * 2 : 0
            visible: root.enumerationErrorMessage.length > 0
            color: Style.formLabelErrorColor
            opacity: 0.9
            
            RowLayout {
                id: errorBannerContent
                anchors.fill: parent
                anchors.margins: Style.spacingMedium
                spacing: Style.spacingSmall
                
                // Warning icon
                Text {
                    text: "⚠"
                    font.pixelSize: Style.fontSizeFormLabel
                    color: "white"
                }
                
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Could not list storage devices: %1").arg(root.enumerationErrorMessage)
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: "white"
                    wrapMode: Text.WordWrap
                }
            }
            
            Accessible.role: Accessible.AlertMessage
            Accessible.name: qsTr("Error: Could not list storage devices. %1").arg(root.enumerationErrorMessage)
        }
        
        // Storage device list fills available space
        SelectionListView {
            id: dstlist
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.imageWriter.getDriveList()
            delegate: dstdelegate
            keyboardAutoAdvance: true
            nextFunction: root.conditionalNext
            isItemSelectableFunction: root.isStorageItemSelectable
            accessibleName: {
                // Count only visible and selectable devices
                var selectableCount = 0
                for (var i = 0; i < dstlist.count; i++) {
                    if (root.isStorageItemSelectable(i)) {
                        selectableCount++
                    }
                }
                
                var baseName = qsTr("Storage device list")
                
                // Add item count (only selectable items)
                if (selectableCount === 0) {
                    baseName += ". " + qsTr("No devices")
                } else if (selectableCount === 1) {
                    baseName += ". " + qsTr("1 device")
                } else {
                    baseName += ". " + qsTr("%1 devices").arg(selectableCount)
                }
                
                baseName += ". " + qsTr("Use arrow keys to navigate, Enter or Space to select")
                
                // Include the status message if there are no valid options
                if (!root.hasValidStorageOptions) {
                    var statusMsg = root.getStorageStatusMessage()
                    baseName += ". " + statusMsg
                }
                return baseName
            }
            accessibleDescription: ""
            
            // Disable ListView's built-in highlight since delegate handles its own highlighting
            highlight: Item {}
            highlightFollowsCurrentItem: false
            
            onItemSelected: function(index, item) {
                if (index >= 0 && index < dstlist.count) {
                    // Try to use the delegate item if available
                    if (item && typeof item.selectDrive === "function") {
                        item.selectDrive()
                    } else {
                        // Fallback: get data directly from the model
                        // This handles cases where itemAtIndex() returns null
                        // (e.g., when using screen readers on Windows where
                        // delegates may not be instantiated)
                        root.selectDriveByIndex(index)
                    }
                }
            }
            
            onItemDoubleClicked: function(index, item) {
                // First select the item
                if (index >= 0 && index < count && item && typeof item.selectDrive === "function") {
                    item.selectDrive()
                    
                    // Then advance to next step if possible (same as pressing Return)
                    Qt.callLater(function() {
                        if (root.nextButtonEnabled) {
                            root.nextClicked()
                        }
                    })
                }
            }

            // Update storage status when device list changes
            onCountChanged: {
                root.updateStorageStatus()
            }
            
            // No storage devices or no valid options message (visually hidden, for screen readers only)
            Label {
                id: noDevicesLabel
                anchors.fill: parent
                visible: !root.hasValidStorageOptions
                text: {
                    // Check for enumeration error first
                    if (root.enumerationErrorMessage.length > 0) {
                        return qsTr("Could not list storage devices: %1").arg(root.enumerationErrorMessage)
                    } else if (!root.hasAnyDevices) {
                        return qsTr("No storage devices found")
                    } else if (root.hasOnlyReadOnlyDevices) {
                        if (filterSystemDrives.checked) {
                            return qsTr("All visible devices are read-only.\nTry connecting a new device, or uncheck\n'Exclude system drives' below.")
                        } else {
                            return qsTr("All devices are read-only.\nPlease connect a writable storage device.")
                        }
                    } else {
                        return qsTr("All devices are hidden by the filter.\nUncheck 'Exclude system drives' below\nto show system drives.")
                    }
                }
                // Make it invisible but still accessible to screen readers
                opacity: 0
                Accessible.role: Accessible.StatusBar
                Accessible.name: text
                Accessible.ignored: false
                
                // Force accessibility update when text changes
                onTextChanged: {
                    if (visible) {
                        // Briefly toggle focus to force screen reader update
                        Accessible.ignored = true
                        Qt.callLater(function() {
                            Accessible.ignored = false
                        })
                    }
                }
                
                // Announce when becomes visible
                onVisibleChanged: {
                    if (visible) {
                        // Small delay to ensure the text is set before announcing
                        Qt.callLater(function() {
                            if (visible) {
                                forceActiveFocus()
                            }
                        })
                    }
                }
            }
        }
        
        // Filter controls
        RowLayout {
            Layout.fillWidth: true
            
            Item {
                Layout.fillWidth: true
            }
            
            ImCheckBox {
                id: filterSystemDrives
                checked: true
                text: qsTr("Exclude system drives")
                Accessible.description: qsTr("When checked, system drives are hidden from the list. Uncheck to show all drives including system drives.")

                onToggled: {
                    if (!checked) {
                        // If warnings are disabled, bypass the confirmation dialog
                        if (root.wizardContainer && root.wizardContainer.disableWarnings) {
                            // Leave checkbox unchecked and continue showing system drives
                            dstlist.forceActiveFocus()
                        } else {
                            // Ask for stern confirmation before disabling filtering
                            confirmUnfilterPopup.open()
                        }
                    }
                }
                
                // Update storage status whenever checked state changes (from any source)
                onCheckedChanged: {
                    // Defer the update to allow the model to update first
                    Qt.callLater(root.updateStorageStatus)
                }
            }
        }
        

    }
    ]
    
    // Storage delegate component
    Component {
        id: dstdelegate
        
        Item {
            id: dstitem
            
            required property int index
            required property string device
            required property string description
            required property string size
            required property bool isUsb
            required property bool isScsi
            required property bool isReadOnly
            required property bool isSystem
            required property var mountpoints
            required property QtObject modelData
            
            readonly property bool shouldHide: isSystem && filterSystemDrives.checked
            readonly property bool unselectable: isReadOnly
            
            // Accessibility properties
            Accessible.role: Accessible.ListItem
            Accessible.name: dstitem.description + ". " + imageWriter.formatSize(parseFloat(dstitem.size)) + (dstitem.mountpoints.length > 0 ? ". " + qsTr("Mounted as %1").arg(dstitem.mountpoints.join(", ")) : "") + (dstitem.unselectable ? ". " + qsTr("Read-only") : "")
            Accessible.focusable: true
            Accessible.ignored: false
            
            // Function called by keyboard selection
            function selectDrive() {
                if (!unselectable) {
                    root.selectDstItem(dstitem)
                }
            }
            
            width: dstlist.width
            height: shouldHide ? 0 : 80
            visible: !shouldHide
            
            Rectangle {
                id: dstbgrect
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: (dstlist.currentIndex === dstitem.index) ? Style.listViewHighlightColor :
                       (dstMouseArea.containsMouse && !dstitem.unselectable ? Style.listViewHoverRowBackgroundColor : Style.listViewRowBackgroundColor)
                radius: 0
                opacity: dstitem.unselectable ? 0.5 : 1.0
                anchors.rightMargin: (dstlist.contentHeight > dstlist.height ? Style.scrollBarWidth : 0)
                Accessible.ignored: true
                
                MouseArea {
                    id: dstMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: dstitem.unselectable ? Qt.ForbiddenCursor : Qt.PointingHandCursor
                    enabled: !dstitem.unselectable
                    
                    onClicked: {
                        if (!dstitem.unselectable) {
                            dstlist.currentIndex = dstitem.index
                            root.selectDstItem(dstitem)
                        }
                    }
                    
                    onDoubleClicked: {
                        if (!dstitem.unselectable) {
                            // Double-click acts like pressing Return - select and advance
                            dstlist.itemDoubleClicked(dstitem.index, dstitem)
                        }
                    }
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Style.listItemPadding
                    spacing: Style.spacingMedium
                    
                    // Storage icon
                    Image {
                        id: storageIcon
                        source: dstitem.isUsb ? "../icons/ic_usb_40px.svg" :
                                dstitem.isScsi ? "../icons/ic_storage_40px.svg" :
                                "../icons/ic_sd_storage_40px.svg"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        // Rasterize vector sources at device pixel ratio to avoid aliasing/blurriness on HiDPI
                        sourceSize: Qt.size(Math.round(40 * Screen.devicePixelRatio), Math.round(40 * Screen.devicePixelRatio))
                        visible: source.toString().length > 0

                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: Style.titleSeparatorColor
                            border.width: 1
                            radius: 0
                            visible: parent.status === Image.Error
                        }
                    }
                    
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.spacingXXSmall
                        
                        MarqueeText {
                            text: dstitem.description
                            font.pixelSize: Style.fontSizeFormLabel
                            font.family: Style.fontFamilyBold
                            font.bold: true
                            color: dstitem.unselectable ? Style.formLabelDisabledColor : Style.formLabelColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        
                        Text {
                            text: imageWriter.formatSize(parseFloat(dstitem.size))
                            font.pixelSize: Style.fontSizeDescription
                            font.family: Style.fontFamily
                            color: dstitem.unselectable ? Style.formLabelDisabledColor : Style.textDescriptionColor
                            Layout.fillWidth: true
                            Accessible.ignored: true
                        }
                        
                        MarqueeText {
                            text: dstitem.mountpoints.length > 0 ? 
                                  qsTr("Mounted as %1").arg(dstitem.mountpoints.join(", ")) : ""
                            font.pixelSize: Style.fontSizeSmall
                            font.family: Style.fontFamily
                            color: dstitem.unselectable ? Style.formLabelDisabledColor : Style.textMetadataColor
                            Layout.fillWidth: true
                            visible: dstitem.mountpoints.length > 0
                            Accessible.ignored: true
                        }
                    }
                    
                    // Read-only indicator
                    Text {
                        text: qsTr("Read-only")
                        font.pixelSize: Style.fontSizeDescription
                        font.family: Style.fontFamily
                        color: Style.formLabelErrorColor
                        visible: dstitem.unselectable
                        Accessible.ignored: true
                    }
                }
            }
        }
    }
    
    // Storage selection function
    function selectDstItem(dstitem) {
        if (dstitem.unselectable) {
            return
        }

        if (dstitem.isSystem) {
            // Show stern confirmation dialog requiring typing the name
            systemDriveConfirm.driveName = dstitem.description
            systemDriveConfirm.device = dstitem.device
            systemDriveConfirm.deviceSize = dstitem.size
            systemDriveConfirm.sizeStr = imageWriter.formatSize(parseFloat(dstitem.size))
            systemDriveConfirm.mountpoints = dstitem.mountpoints
            systemDriveConfirm.open()
            return
        }

        imageWriter.setDst(dstitem.device, dstitem.size)
        selectedDeviceName = dstitem.description
        root.wizardContainer.selectedStorageName = dstitem.description

        // Do not auto-advance; enable Next
        root.nextButtonEnabled = true
    }
    
    // Select drive by model index (used when delegate item is not available,
    // e.g., when using screen readers where itemAtIndex() returns null)
    function selectDriveByIndex(index) {
        var model = root.imageWriter.getDriveList()
        if (!model || index < 0 || index >= model.rowCount()) {
            return
        }
        
        var modelIndex = model.index(index, 0)
        if (!modelIndex || !modelIndex.valid) {
            return
        }
        
        // Role values from DriveListModel (Qt::UserRole + 1, etc.)
        var deviceRole = 0x101
        var descriptionRole = 0x102
        var sizeRole = 0x103
        var isReadOnlyRole = 0x106
        var isSystemRole = 0x107
        var mountpointsRole = 0x108
        
        var isReadOnly = model.data(modelIndex, isReadOnlyRole)
        if (isReadOnly) {
            return  // Can't select read-only devices
        }
        
        var isSystem = model.data(modelIndex, isSystemRole)
        var device = model.data(modelIndex, deviceRole)
        var description = model.data(modelIndex, descriptionRole)
        var size = model.data(modelIndex, sizeRole)
        var mountpoints = model.data(modelIndex, mountpointsRole) || []
        
        // Create a mock item object with the required properties
        var mockItem = {
            unselectable: isReadOnly,
            isSystem: isSystem,
            device: device,
            description: description,
            size: size,
            mountpoints: mountpoints
        }
        
        root.selectDstItem(mockItem)
    }

    // Check if a storage item at the given index is selectable (not read-only and not hidden)
    function isStorageItemSelectable(index) {
        var model = root.imageWriter.getDriveList()
        if (!model || index < 0 || index >= model.rowCount()) {
            return false
        }
        
        var isReadOnlyRole = 0x106
        var isSystemRole = 0x107
        
        var idx = model.index(index, 0)
        var isReadOnly = model.data(idx, isReadOnlyRole)
        var isSystem = model.data(idx, isSystemRole)
        
        // Item is selectable if it's not read-only and either not a system drive or filter is off
        var shouldHide = isSystem && filterSystemDrives.checked
        return !isReadOnly && !shouldHide
    }
    
    // Check if there are any selectable items in the list
    function hasSelectableItems() {
        var model = root.imageWriter.getDriveList()
        if (!model || model.rowCount() === 0) {
            return false
        }
        
        for (var i = 0; i < model.rowCount(); i++) {
            if (isStorageItemSelectable(i)) {
                return true
            }
        }
        return false
    }
    
    // Update storage status properties for accessibility messages
    function updateStorageStatus() {
        var model = root.imageWriter.getDriveList()
        root.hasValidStorageOptions = hasSelectableItems()
        root.hasAnyDevices = model && model.rowCount() > 0
        
        // Check if we only have read-only devices
        if (root.hasAnyDevices && !root.hasValidStorageOptions) {
            var isReadOnlyRole = 0x106
            var allReadOnly = true
            for (var i = 0; i < model.rowCount(); i++) {
                var idx = model.index(i, 0)
                var isReadOnly = model.data(idx, isReadOnlyRole)
                if (!isReadOnly) {
                    allReadOnly = false
                    break
                }
            }
            root.hasOnlyReadOnlyDevices = allReadOnly
        } else {
            root.hasOnlyReadOnlyDevices = false
        }
    }
    
    // Get the storage status message for accessibility
    function getStorageStatusMessage() {
        // Check for enumeration error first
        if (root.enumerationErrorMessage.length > 0) {
            return qsTr("Could not list storage devices: %1. This may be a permissions issue. Try running the application with administrator privileges.").arg(root.enumerationErrorMessage)
        } else if (!root.hasAnyDevices) {
            return qsTr("No storage devices found. Please connect a storage device to continue.")
        } else if (root.hasOnlyReadOnlyDevices) {
            if (filterSystemDrives.checked) {
                return qsTr("No valid storage devices are currently available. All visible devices are read-only. Try connecting a new storage device, or uncheck 'Exclude system drives' to show hidden system drives.")
            } else {
                return qsTr("No valid storage devices are currently available. All devices are read-only. Please connect a writable storage device to continue.")
            }
        } else {
            return qsTr("No valid storage devices are currently available. Uncheck 'Exclude system drives' to show hidden system drives, or connect a new storage device.")
        }
    }
    // Stern confirmation when disabling system drive filtering
    ConfirmUnfilterDialog {
        id: confirmUnfilterPopup
        imageWriter: root.imageWriter
        overlayParent: root.wizardContainer && root.wizardContainer.overlayRootRef ? root.wizardContainer.overlayRootRef : (root.Window.window ? root.Window.window.overlayRootItem : null)
        onConfirmed: {
            // user chose to disable filter; leave checkbox unchecked
            // updateStorageStatus will be called via onCheckedChanged
        }
        onCancelled: {
            // Re-enable the filter checkbox and keep system drives hidden
            filterSystemDrives.checked = true
            // updateStorageStatus will be called via onCheckedChanged
        }
        onClosed: {
            if (filterSystemDrives.checked === false) {
                dstlist.forceActiveFocus()
            } else {
                filterSystemDrives.forceActiveFocus()
            }
        }
    }

    // Confirmation when selecting a system drive: type the exact name
    ConfirmSystemDriveDialog {
        id: systemDriveConfirm
        imageWriter: root.imageWriter
        overlayParent: root.wizardContainer && root.wizardContainer.overlayRootRef ? root.wizardContainer.overlayRootRef : (root.Window.window ? root.Window.window.overlayRootItem : null)
        onConfirmed: {
            root.imageWriter.setDst(systemDriveConfirm.device, systemDriveConfirm.deviceSize)
            root.selectedDeviceName = systemDriveConfirm.driveName
            root.wizardContainer.selectedStorageName = systemDriveConfirm.driveName
            // Re-enable filtering after selection via confirmation path
            filterSystemDrives.checked = true
            // updateStorageStatus will be called via onCheckedChanged
            // Enable Next
            root.nextButtonEnabled = true
            // Auto-advance after explicit confirmation on a system drive
            root.next()
        }
        onCancelled: {
            // no-op
        }
    }
} 
