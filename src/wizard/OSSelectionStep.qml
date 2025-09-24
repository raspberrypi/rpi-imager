/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import QtCore
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    readonly property HWListModel hwmodel: imageWriter.getHWList()
    readonly property OSListModel osmodel: imageWriter.getOSList()
    
    title: qsTr("Choose operating system")
    subtitle: qsTr("Select an operating system to install on your Raspberry Pi")
    showNextButton: true
    // Disable Next until a concrete OS has been selected
    nextButtonEnabled: oslist.currentIndex !== -1 && wizardContainer.selectedOsName.length > 0
    
    property alias oslist: oslist
    property alias osswipeview: osswipeview
    property string categorySelected: ""
    property bool modelLoaded: false
    // Track if a custom local image has been chosen in this step
    property bool customSelected: false
    property real customSelectedSize: 0
    
    // Property to trigger cache status re-evaluation when cache changes
    property int cacheStatusVersion: 0
    
    // Connect to cache status changes
    Connections {
        target: imageWriter
        function onCacheStatusChanged() {
            root.cacheStatusVersion++
        }
    }
    
    signal updatePopupRequested(var url)
    signal defaultEmbeddedDriveRequested(var drive)
    
    // Forward the nextClicked signal as next() function
    function next() {
        root.nextClicked()
    }
    
    // Common handler functions for OS selection
    function handleOSSelection(modelData, fromKeyboard) {
        if (fromKeyboard === undefined) {
            fromKeyboard = true  // Default to keyboard since this is called from keyboard handlers
        }
        
        // Check if this is a sublist item - if so, navigate to it
        if (modelData && root.isOSsublist(modelData)) {
            root.selectOSitem(modelData, true, false)
        } else {
            // Regular OS selection
            root.selectOSitem(modelData, false, false)
            
            // For keyboard selection of concrete OS items, automatically advance to next step
            if (fromKeyboard && modelData && !root.isOSsublist(modelData)) {
                // Use Qt.callLater to ensure the OS selection completes first
                Qt.callLater(function() {
                    if (root.nextButtonEnabled) {
                        root.next()
                    }
                })
            }
        }
    }
    
    function handleOSNavigation(modelData) {
        // Right arrow key: only navigate to sublists, ignore regular OS items
        if (modelData && root.isOSsublist(modelData)) {
            root.selectOSitem(modelData, true, false)
        }
    }
    
    function handleBackNavigation() {
        osswipeview.decrementCurrentIndex()
        root.categorySelected = ""
        // Rebuild focus order when returning to main list
        root.rebuildFocusOrder()
    }
    
    function initializeListViewFocus(listView) {
        // Ensure we have an initial selection for keyboard navigation
        if (listView.currentIndex === -1 && listView.count > 0) {
            listView.currentIndex = 0
        }
    }

    Component.onCompleted: {
        // Try initial load in case data is already available
        onOsListPreparedHandler()

        // Register the OS list for keyboard navigation
        root.registerFocusGroup("os_list", function(){
            // Only include the currently active list view
            var currentPage = osswipeview.itemAt(osswipeview.currentIndex)
            return currentPage ? [currentPage] : [oslist]
        }, 0)

        // Set the initial focus item to the OS list
        root.initialFocusItem = oslist

        // Ensure focus starts on the OS list when entering this step
        oslist.forceActiveFocus()
        _focusFirstItemInCurrentView()
    }

    Connections {
        target: imageWriter
        function onOsListPrepared() {
            // Prefer surgical refresh to avoid stealing focus during clicks
            if (root.modelLoaded && root.osmodel && typeof root.osmodel.softRefresh === "function") {
                root.osmodel.softRefresh()
            } else {
                onOsListPreparedHandler()
            }
        }
        // Handle native file selection for "Use custom"
        function onFileSelected(fileUrl) {
            // Ensure ImageWriter src is set to the chosen file explicitly
            imageWriter.setSrc(fileUrl)
            // Update selected OS name to the chosen file name
            root.wizardContainer.selectedOsName = imageWriter.srcFileName()
            root.wizardContainer.customizationSupported = imageWriter.imageSupportsCustomization()
            // For custom images, customization is not supported; clear any staged flags
            if (!root.wizardContainer.customizationSupported) {
                root.wizardContainer.hostnameConfigured = false
                root.wizardContainer.localeConfigured = false
                root.wizardContainer.userConfigured = false
                root.wizardContainer.wifiConfigured = false
                root.wizardContainer.sshEnabled = false
                root.wizardContainer.piConnectEnabled = false
                root.wizardContainer.piConnectAvailable = false
            }
            root.customSelected = true
            root.customSelectedSize = imageWriter.getSelectedSourceSize()
            root.nextButtonEnabled = true
        }
    }

    // Fallback custom FileDialog (styled) when native dialogs are unavailable
    // Exposed via property alias for callsite access
    property alias customImageFileDialog: customImageFileDialog
    ImFileDialog {
        id: customImageFileDialog
        nameFilters: CommonStrings.imageFiltersList
        onAccepted: {
            imageWriter.acceptCustomImageFromQml(selectedFile)
        }
        onRejected: {
            // No-op; user cancelled
        }
    }
    
    // No-op (previous auto-focus/auto-select logic removed to avoid stealing click timing)
    function _focusFirstItemInCurrentView() {}
    
    // Ensure the clicked selection is visually highlighted in the current list view
    function _highlightMatchingEntryInCurrentView(selectedModel) {
        var currentView = osswipeview.currentItem
        if (!currentView || !currentView.model || typeof currentView.model.get !== "function") {
            return
        }
        for (var i = 0; i < currentView.count; i++) {
            var entry = currentView.model.get(i)
            if (entry && entry.name === selectedModel.name && entry.url === selectedModel.url) {
                currentView.currentIndex = i
                break
            }
        }
    }
    
    // Content
    content: [
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // OS selection area - fill available space without extra chrome/padding
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            // OS SwipeView for navigation between categories
            SwipeView {
                id: osswipeview
                anchors.fill: parent
                interactive: false
                clip: true

                onCurrentIndexChanged: {
                    _focusFirstItemInCurrentView()
                    // Rebuild focus order when switching between main list and sublists
                    root.rebuildFocusOrder()
                    // Ensure the current view gets focus when SwipeView index changes
                    Qt.callLater(function() {
                        var currentView = osswipeview.currentItem
                        if (currentView && typeof currentView.forceActiveFocus === "function") {
                            currentView.forceActiveFocus()
                        }
                    })
                }
                    
                    // Main OS list
                    OSSelectionListView {
                        id: oslist
                        model: root.osmodel
                        delegate: osdelegate
                        autoSelectFirst: true
                        
                        // Connect to our OS selection handler
                        osSelectionHandler: root.handleOSSelection
                        
                        onRightPressed: function(index, item, modelData) {
                            root.handleOSNavigation(modelData)
                        }
                        
                        Component.onCompleted: {
                            root.initializeListViewFocus(oslist)
                        }
                        
                        onCountChanged: {
                            root.initializeListViewFocus(oslist)
                        }
                    }
            }
        }
    }
    ]
    
    // OS delegate component
    Component {
        id: osdelegate
        
        Item {
            id: delegateItem
            
            required property int index
            required property string name
            required property string description
            required property string icon
            required property string release_date
            required property string url
            required property string subitems_json
            required property string extract_sha256
            required property QtObject model
            required property double image_download_size
            required property var capabilities
            
            property string website
            property string tooltip
            property string subitems_url
            
            // Get reference to the containing ListView
            // IMPORTANT: Cache ListView.view in a property for reliable access.
            // This delegate is shared between the main OS list and dynamically created sublists.
            // Using ListView.view directly in bindings can be unreliable, so we cache it here.
            // This enables proper highlighting for both keyboard and mouse navigation in all lists.
            property var parentListView: ListView.view
            
            width: parentListView ? parentListView.width : 200
            // Let content determine height for balanced vertical padding
            height: Math.max(80, row.implicitHeight + Style.spacingSmall + Style.spacingMedium)
            
            Rectangle {
                id: osbgrect
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                // Delegate highlighting: Works together with ListView's built-in highlight system
                // DO NOT disable ListView's highlight - both systems work in harmony
                color: (parentListView && parentListView.currentIndex === index) ? Style.listViewHighlightColor :
                       (osMouseArea.containsMouse ? Style.listViewHoverRowBackgroundColor : Style.listViewRowBackgroundColor)
                radius: 0
                anchors.rightMargin: (
                    (parentListView && parentListView.contentHeight > parentListView.height) ? Style.scrollBarWidth : 0)
                
                MouseArea {
                    id: osMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.PointingHandCursor
                    
                    onPressed: {
                        if (parentListView) {
                            if (!parentListView.activeFocus) {
                                parentListView.forceActiveFocus()
                            }
                            parentListView.currentIndex = index
                        }
                    }

                    onClicked: {
                        // currentIndex is set on press; ensure selection is in place
                        if (parentListView && parentListView.currentIndex !== index) {
                            parentListView.currentIndex = index
                        }
                        selectOSitem(delegateItem.model, undefined, true)
                    }
                }
                
                RowLayout {
                    id: row
                    anchors.fill: parent
                    anchors.leftMargin: Style.listItemPadding
                    anchors.rightMargin: Style.listItemPadding
                    anchors.topMargin: Style.spacingSmall
                    anchors.bottomMargin: Style.spacingMedium
                    spacing: Style.spacingMedium
                    
                    // OS Icon
                    Image {
                        id: osicon
                        source: delegateItem.icon
                        cache: true
                        asynchronous: true
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                        // Rasterize vector sources at device pixel ratio to avoid aliasing/blurriness on HiDPI
                        sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio), Math.round(height * Screen.devicePixelRatio))
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
                        
                        Text {
                            text: delegateItem.name
                            font.pixelSize: Style.fontSizeFormLabel
                            font.family: Style.fontFamilyBold
                            font.bold: true
                            color: Style.formLabelColor
                            Layout.fillWidth: true
                        }
                        
                        Text {
                            text: delegateItem.description
                            font.pixelSize: Style.fontSizeDescription
                            font.family: Style.fontFamily
                            color: Style.textDescriptionColor
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        // Cache/local/online status line
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Style.textMetadataColor
                            font.family: Style.fontFamily
                            // Hide for custom until a file is chosen; otherwise show status
                            visible: (typeof(delegateItem.url) === "string" && delegateItem.url !== "internal://custom" && delegateItem.url !== "internal://format")
                                     || (typeof(delegateItem.url) === "string" && delegateItem.url === "internal://custom" && root.customSelected)
                            text:
                                (typeof(delegateItem.url) === "string" && delegateItem.url === "internal://custom")
                                ? (function(){
                                    var sz = root.customSelected ? root.customSelectedSize : 0;
                                    return sz > 0 ? qsTr("Local - %1").arg(imageWriter.formatSize(sz)) : "";
                                  })()
                                : (!delegateItem.url ? "" :
                                  ((typeof(delegateItem.extract_sha256) !== "undefined" && 
                                    root.cacheStatusVersion >= 0 && // Force re-evaluation when cache status changes
                                    imageWriter.isCached(delegateItem.url, delegateItem.extract_sha256))
                                    ? qsTr("Cached on your computer")
                                    : (delegateItem.url.startsWith("file://")
                                       ? qsTr("Local file")
                                       : qsTr("Online - %1 download").arg(imageWriter.formatSize(delegateItem.image_download_size)))))
                        }
                        
                        Text {
                            text: delegateItem.release_date
                            font.pixelSize: Style.fontSizeSmall
                            font.family: Style.fontFamily
                            color: Style.textMetadataColor
                            Layout.fillWidth: true
                            visible: delegateItem.release_date !== ""
                        }
                    }
                }
            }
        }
    }
    
    // Sublist page component
    Component {
        id: suboslist
        
        OSSelectionListView {
            id: sublistview
            autoSelectFirst: true
            model: ListModel {
                // Back entry
                ListElement {
                    url: ""
                    icon: "../icons/ic_chevron_left_40px.svg"
                    extract_size: 0
                    image_download_size: 0
                    extract_sha256: ""
                    contains_multiple_files: false
                    release_date: ""
                    subitems_url: "internal://back"
                    subitems_json: ""
                    name: qsTr("Back")
                    description: qsTr("Go back to main menu")
                    tooltip: ""
                    website: ""
                    init_format: ""
                    capabilities: ""
                }
            }
            delegate: osdelegate
            
            // Connect to our OS selection handler
            osSelectionHandler: root.handleOSSelection
            
            onRightPressed: function(index, item, modelData) {
                root.handleOSNavigation(modelData)
            }
            
            onLeftPressed: root.handleBackNavigation
            
            Component.onCompleted: {
                // Ensure this sublist can receive keyboard focus
                forceActiveFocus()
                root.initializeListViewFocus(sublistview)
            }
        }
    }

    // OS selection functions (adapted from OSPopup.qml)
    function selectOSitem(model, navigateOnly, fromMouse) {
        if (navigateOnly === undefined) {
            navigateOnly = false
        }
        if (fromMouse === undefined) {
            fromMouse = false
        }
        
        if (isOSsublist(model)) {
            // Navigate to sublist (whether navigateOnly is true or false)
            categorySelected = model.name
            var lm = newSublist()
            populateSublistInto(lm, model)
            // focus first sub item when navigating
            var nextView = osswipeview.itemAt(osswipeview.currentIndex+1)
            osswipeview.incrementCurrentIndex()
            // ensure focus is on the new view and set currentIndex
            _focusFirstItemInCurrentView()
            // Force currentIndex and focus on the new sublist
            Qt.callLater(function() {
                var currentView = osswipeview.currentItem
                if (currentView) {
                    // Always set currentIndex to 0 for sublists to ensure highlighting
                    currentView.currentIndex = 0
                    if (typeof currentView.forceActiveFocus === "function") {
                        currentView.forceActiveFocus()
                    }
                }
            })
        } else {
            // Select this OS - explicit branching for clarity
            if (typeof(model.url) === "string" && model.url === "internal://custom") {
                // Use custom: open native file selector if available, otherwise fall back to QML FileDialog
                root.wizardContainer.selectedOsName = ""
                root.nextButtonEnabled = false
                root.customSelected = false
                if (imageWriter.nativeFileDialogAvailable()) {
                    // Defer opening the native dialog until after the current event completes
                    Qt.callLater(function() {
                        imageWriter.openFileDialog(
                            qsTr("Select image"),
                            CommonStrings.imageFiltersString)
                    })
                } else if (root.hasOwnProperty("customImageFileDialog")) {
                    // Ensure reasonable defaults
                    customImageFileDialog.dialogTitle = qsTr("Select image")
                    customImageFileDialog.nameFilters = CommonStrings.imageFiltersList
                    // Default to Downloads folder
                    var dl = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
                    if (dl && dl.length > 0) {
                        var furl = (Qt.platform.os === "windows") ? ("file:///" + dl) : ("file://" + dl)
                        customImageFileDialog.currentFolder = furl
                        customImageFileDialog.folder = furl
                    }
                    customImageFileDialog.open()
                } else {
                    console.warn("OSSelectionStep: No FileDialog fallback available")
                }
            } else if (typeof(model.url) === "string" && model.url === "internal://format") {
                // Erase/format flow
                imageWriter.setSrc(
                    model.url,
                    model.image_download_size,
                    model.extract_size,
                    typeof(model.extract_sha256) != "undefined" ? model.extract_sha256 : "",
                    typeof(model.contains_multiple_files) != "undefined" ? model.contains_multiple_files : false,
                    categorySelected,
                    model.name,
                    typeof(model.init_format) != "undefined" ? model.init_format : ""
                )
                imageWriter.setSWCapabilitiesList("[]")

                root.wizardContainer.selectedOsName = model.name
                root.wizardContainer.customizationSupported = imageWriter.imageSupportsCustomization()
                // Gate Raspberry Pi Connect availability by OS JSON field
                root.wizardContainer.piConnectAvailable = (typeof(model.enable_rpi_connect) !== "undefined") ? !!model.enable_rpi_connect : false
                root.wizardContainer.rpiosCloudInitAvailable = false
                root.nextButtonEnabled = true
                if (fromMouse) {
                    Qt.callLater(function() { _highlightMatchingEntryInCurrentView(model) })
                }
            } else {
                // Normal OS selection
                imageWriter.setSrc(
                    model.url,
                    model.image_download_size,
                    model.extract_size,
                    typeof(model.extract_sha256) != "undefined" ? model.extract_sha256 : "",
                    typeof(model.contains_multiple_files) != "undefined" ? model.contains_multiple_files : false,
                    categorySelected,
                    model.name,
                    typeof(model.init_format) != "undefined" ? model.init_format : ""
                )
                imageWriter.setSWCapabilitiesList(model.capabilities)

                root.wizardContainer.selectedOsName = model.name
                root.wizardContainer.customizationSupported = imageWriter.imageSupportsCustomization()
                // Gate Raspberry Pi Connect availability by OS JSON field
                root.wizardContainer.piConnectAvailable = (typeof(model.enable_rpi_connect) !== "undefined") ? !!model.enable_rpi_connect : false
                root.wizardContainer.rpiosCloudInitAvailable = imageWriter.checkSWCapability("rpios_cloudinit")
                // If customization is not supported for this OS, clear any previously-staged UI flags
                if (!root.wizardContainer.customizationSupported) {
                    root.wizardContainer.hostnameConfigured = false
                    root.wizardContainer.localeConfigured = false
                    root.wizardContainer.userConfigured = false
                    root.wizardContainer.wifiConfigured = false
                    root.wizardContainer.sshEnabled = false
                    root.wizardContainer.piConnectEnabled = false
                } else if (!root.wizardContainer.piConnectAvailable) {
                    // If Raspberry Pi Connect not available for this OS, ensure it's not marked enabled
                    root.wizardContainer.piConnectEnabled = false
                }
                root.customSelected = false
                root.nextButtonEnabled = true
                if (fromMouse) {
                    Qt.callLater(function() { _highlightMatchingEntryInCurrentView(model) })
                }
            }

            if (model.subitems_url === "internal://back") {
                osswipeview.decrementCurrentIndex()
                categorySelected = ""
            } else {
                // Stay on page; user must click Next
            }
        }
    }
    
    function isOSsublist(model) {
        // Properly handle undefined/null values
        var jsonType = typeof(model.subitems_json)
        var jsonNotEmpty = model.subitems_json !== ""
        var hasSubitemsJson = (jsonType == "string" && jsonNotEmpty)
        
        var urlType = typeof(model.subitems_url)
        var urlNotEmpty = model.subitems_url !== ""
        var urlNotBack = model.subitems_url !== "internal://back"
        var hasSubitemsUrl = (urlType == "string" && urlNotEmpty && urlNotBack)
        
        var isSublist = hasSubitemsJson || hasSubitemsUrl
        
        return isSublist
    }
    
    // Add or reuse sublist page and return its ListModel
    function newSublist() {
        if (osswipeview.currentIndex === (osswipeview.count - 1)) {
            var newlist = suboslist.createObject(osswipeview)
            osswipeview.addItem(newlist)
            // Rebuild focus order to include the new sublist
            root.rebuildFocusOrder()
        }
        var m = osswipeview.itemAt(osswipeview.currentIndex+1).model
        if (m.count > 1) {
            m.remove(1, m.count - 1)
        }
        return m
    }

    // Populate given ListModel from a model's subitems_json, flattening nested items
    function populateSublistInto(listModel, model) {
        if (typeof(model.subitems_json) !== "string" || model.subitems_json === "") {
            return
        }
        var subitems = []
        try {
            subitems = JSON.parse(model.subitems_json)
        } catch (e) {
            console.log("Failed to parse subitems_json:", e)
            return
        }
        for (var i in subitems) {
            var entry = subitems[i]
            if (entry && typeof(entry) === "object") {
                if ("subitems" in entry) {
                    entry["subitems_json"] = JSON.stringify(entry["subitems"])
                    delete entry["subitems"]
                }
                // Propagate init_format from parent when missing so customization remains available
                if (typeof(entry.init_format) === "undefined" && typeof(model.init_format) === "string" && model.init_format !== "") {
                    entry.init_format = model.init_format
                }
                if (typeof(entry.icon) === "string" && entry.icon.indexOf("icons/") === 0) {
                    entry.icon = "../" + entry.icon
                }
                // Ensure role types remain consistent across the ListModel
                entry.url = String(entry.url || "")
                entry.icon = String(entry.icon || "")
                entry.subitems_url = String(entry.subitems_url || "")
                entry.website = String(entry.website || "")
                // Propagate enable_rpi_connect from parent when missing
                if (typeof(entry.enable_rpi_connect) === "undefined" && typeof(model.enable_rpi_connect) !== "undefined") {
                    entry.enable_rpi_connect = model.enable_rpi_connect
                }

                if (typeof entry.capabilities === "string") {
                    // keep it
                } else if (Array.isArray(entry.capabilities)) {
                    entry.capabilities = JSON.stringify(entry.capabilities);
                } else if (entry.capabilities && typeof entry.capabilities.length === "number") {
                    // QVariantList-looking object
                    entry.capabilities = JSON.stringify(Array.prototype.slice.call(entry.capabilities));
                } else {
                    entry.capabilities = "[]";
                }

                listModel.append(entry)
            }
        }
    }
    
    // Called when OS list data is ready from network
    function onOsListPreparedHandler() {
        if (!root || !root.osmodel) {
            return
        }
        // Always reload to reflect cache status changes and updates from backend
        console.log("OSSelectionStep: osListPrepared received - reloading model")
        var osSuccess = root.osmodel.reload()
        if (osSuccess && !modelLoaded) {
            modelLoaded = true
            var o = JSON.parse(root.imageWriter.getFilteredOSlist())
            if ("imager" in o) {
                var imager = o["imager"]
                if (root.imageWriter.getBoolSetting("check_version") && "latest_version" in imager && "url" in imager) {
                    if (!root.imageWriter.isEmbeddedMode() && root.imageWriter.isVersionNewer(imager["latest_version"])) {
                        root.updatePopupRequested(imager["url"])
                    }
                }
                if ("default_os" in imager) {
                    selectNamedOS(imager["default_os"], root.osmodel)
                }
                if (root.imageWriter.isEmbeddedMode()) {
                    if ("embedded_default_os" in imager) {
                        selectNamedOS(imager["embedded_default_os"], root.osmodel)
                    }
                    if ("embedded_default_destination" in imager) {
                        root.defaultEmbeddedDriveRequested(imager["embedded_default_destination"])
                    }
                }
            }
        }
    }
    
    function selectNamedOS(osName, model) {
        for (var i = 0; i < model.rowCount(); i++) {
            var entry = model.get(i)
            if (entry && entry.name === osName) {
                selectOSitem(entry)
                break
            }
        }
    }
} 
