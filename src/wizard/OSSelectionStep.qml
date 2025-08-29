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
    
    title: qsTr("Choose Operating System")
    subtitle: qsTr("Select the operating system to install on your Raspberry Pi")
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
    
    signal updatePopupRequested(var url)
    signal defaultEmbeddedDriveRequested(var drive)
    
    // Forward the nextClicked signal as next() function
    function next() {
        root.nextClicked()
    }

    function getNextFocusableElement(startElement) {
        if (!startElement || !startElement.visible) startElement = osswipeview;
        var order = [ osswipeview, nextButtonItem, backButtonItem ]
        var currentIndex = order.indexOf(startElement)
        if (currentIndex === -1) return osswipeview;
        var nextElement = order[(currentIndex + 1) % order.length]
        return nextElement.visible && nextElement.enabled ? nextElement : startElement
    }

    function getPreviousFocusableElement(startElement) {
        if (!startElement || !startElement.visible) startElement = osswipeview;
        var order = [ osswipeview, nextButtonItem, backButtonItem ]
        var currentIndex = order.indexOf(startElement)
        if (currentIndex === -1) return osswipeview;
        var prevElement = order[(currentIndex - 1 + order.length) % order.length]
        return prevElement.visible && prevElement.enabled ? prevElement : startElement
    }

    Component.onCompleted: {
        // Try initial load in case data is already available
        onOsListPreparedHandler()

        // Set the initial focus item to the SwipeView
        root.initialFocusItem = osswipeview

        // Ensure focus starts on the OS list when entering this step
        osswipeview.forceActiveFocus()
        _focusFirstItemInCurrentView()
    }

    Connections {
        target: imageWriter
        function onOsListPrepared() {
            onOsListPreparedHandler()
        }
        // Handle native file selection for "Use custom"
        function onFileSelected(fileUrl) {
            // Ensure ImageWriter src is set to the chosen file explicitly
            imageWriter.setSrc(fileUrl)
            // Update selected OS name to the chosen file name
            root.wizardContainer.selectedOsName = imageWriter.srcFileName()
            root.wizardContainer.customizationSupported = imageWriter.imageSupportsCustomization()
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
        onAccepted: {
            imageWriter.acceptCustomImageFromQml(selectedFile)
        }
        onRejected: {
            // No-op; user cancelled
        }
    }
    
    // Ensure the current list view has focus and a valid currentIndex
    function _focusFirstItemInCurrentView() {
        var currentView = osswipeview.currentItem
        if (osswipeview.activeFocus && currentView && currentView.count > 0 && currentView.currentIndex === -1) {
            currentView.currentIndex = 0
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
                activeFocusOnTab: true

                onActiveFocusChanged: _focusFirstItemInCurrentView()
                onCurrentIndexChanged: {
                    _focusFirstItemInCurrentView()
                    // Ensure the current view gets focus when SwipeView index changes
                    Qt.callLater(function() {
                        var currentView = osswipeview.currentItem
                        if (currentView && typeof currentView.forceActiveFocus === "function") {
                            currentView.forceActiveFocus()
                        }
                    })
                }
                    
                    // Main OS list
                    ListView {
                        id: oslist
                        model: root.osmodel
                        currentIndex: -1
                        delegate: osdelegate
                        clip: true
                        activeFocusOnTab: true
                        
                        boundsBehavior: Flickable.StopAtBounds
                        // No highlight property needed - using delegate-based highlighting
                        
                        ScrollBar.vertical: ScrollBar {
                            width: Style.scrollBarWidth
                            policy: oslist.contentHeight > oslist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        }
                        
                        Keys.onUpPressed: {
                            if (currentIndex > 0) currentIndex = currentIndex - 1
                        }
                        Keys.onDownPressed: {
                            if (currentIndex < count - 1) currentIndex = currentIndex + 1
                        }
                        Keys.onSpacePressed: {
                            if (currentIndex != -1) {
                                var item = oslist.itemAtIndex(currentIndex)
                                if (item)
                                    root.selectOSitem(item.model, true)
                            }
                        }
                        Accessible.onPressAction: {
                            if (currentIndex != -1) {
                                var item = oslist.itemAtIndex(currentIndex)
                                if (item)
                                    root.selectOSitem(item.model, true)
                            }
                        }

                        Keys.onRightPressed: {
                            // Navigate into sublists but don't select an OS entry
                            if (currentIndex != -1) {
                                var item = oslist.itemAtIndex(currentIndex)
                                if (item && root.isOSsublist(item.model))
                                    root.selectOSitem(item.model, true)
                            }
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
            
            property string website
            property string tooltip
            property string subitems_url
            
            width: oslist.width
            // Let content determine height for balanced vertical padding
            height: Math.max(80, row.implicitHeight + Style.spacingSmall + Style.spacingMedium)
            
            Rectangle {
                id: osbgrect
                anchors.fill: parent
                color: (parent.ListView.view && parent.ListView.view.currentIndex === index) ? Style.listViewHighlightColor :
                       (osMouseArea.containsMouse ? Style.listViewHoverRowBackgroundColor : Style.listViewRowBackgroundColor)
                radius: 0
                
                MouseArea {
                    id: osMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    
                    onClicked: {
                        // Set currentIndex on the correct ListView (main list or sublist)
                        if (parent.ListView.view) {
                            parent.ListView.view.currentIndex = index
                        }
                        selectOSitem(delegateItem.model)
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
                        source: delegateItem.icon
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        fillMode: Image.PreserveAspectFit
                        
                        Rectangle {
                            anchors.fill: parent
                            color: Style.listViewRowBackgroundColor
                            visible: parent.status === Image.Error
                            radius: 0
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
                                  ((typeof(delegateItem.extract_sha256) !== "undefined" && imageWriter.isCached(delegateItem.url, delegateItem.extract_sha256))
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
        
        ListView {
            id: sublistview
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
                }
            }
            currentIndex: -1
            delegate: osdelegate
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // No highlight property needed - using delegate-based highlighting
            ScrollBar.vertical: ScrollBar {
                width: Style.scrollBarWidth
                policy: sublistview.contentHeight > parent.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }
            activeFocusOnTab: true
            
            Keys.onUpPressed: {
                if (currentIndex > 0) {
                    currentIndex--
                }
            }
            Keys.onDownPressed: {
                if (currentIndex < count - 1) {
                    currentIndex++
                }
            }
            Keys.onSpacePressed: {
                if (currentIndex != -1)
                    root.selectOSitem(model.get(currentIndex))
            }
            Accessible.onPressAction: {
                if (currentIndex != -1)
                    root.selectOSitem(model.get(currentIndex))
            }
            Keys.onEnterPressed: (event) => { Keys.spacePressed(event) }
            Keys.onReturnPressed: (event) => { Keys.spacePressed(event) }
            Keys.onRightPressed: {
                // Navigate into sublists but don't select an OS entry
                if (currentIndex != -1 && root.isOSsublist(model.get(currentIndex)))
                    root.selectOSitem(model.get(currentIndex), true)
            }
            Keys.onLeftPressed: {
                osswipeview.decrementCurrentIndex()
                root.categorySelected = ""
            }
            
            Component.onCompleted: {
                // Ensure this sublist can receive keyboard focus
                forceActiveFocus()
                // Set initial currentIndex if not set
                if (currentIndex === -1 && count > 0) {
                    currentIndex = 0
                }
            }
        }
    }

    // OS selection functions (adapted from OSPopup.qml)
    function selectOSitem(model, navigateOnly) {
        if (navigateOnly === undefined) {
            navigateOnly = false
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
                    imageWriter.openFileDialog()
                } else if (root.hasOwnProperty("customImageFileDialog")) {
                    // Ensure reasonable defaults
                    customImageFileDialog.dialogTitle = qsTr("Select image")
                    customImageFileDialog.nameFilters = [
                        "Image files (*.img *.zip *.iso *.gz *.xz *.zst *.wic)",
                        "All files (*)"
                    ]
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
                root.wizardContainer.selectedOsName = model.name
                root.wizardContainer.customizationSupported = imageWriter.imageSupportsCustomization()
                root.nextButtonEnabled = true
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
                root.wizardContainer.selectedOsName = model.name
                root.wizardContainer.customizationSupported = imageWriter.imageSupportsCustomization()
                root.customSelected = false
                root.nextButtonEnabled = true
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