/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
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
    
    Component.onCompleted: {
        // Try initial load in case data is already available
        onOsListPreparedHandler()

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

    // Ensure the current list view has focus and a valid currentIndex
    function _focusFirstItemInCurrentView() {
        var currentView = osswipeview.currentItem
        if (currentView) {
            if (typeof currentView.currentIndex !== "undefined" && currentView.currentIndex === -1 && currentView.count > 0) {
                currentView.currentIndex = 0
                // If this is the main OS list and nothing is selected yet, select the first item implicitly
                if (currentView === oslist && (!wizardContainer.selectedOsName || wizardContainer.selectedOsName.length === 0)) {
                    var firstItem = oslist.itemAtIndex(0)
                    if (firstItem && firstItem.model) {
                        // Do not auto-advance; just set selection/state
                        selectOSitem(firstItem.model)
                    }
                }
            }
            if (typeof currentView.forceActiveFocus === "function") {
                currentView.forceActiveFocus()
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

                    onActiveFocusChanged: _focusFirstItemInCurrentView()
                    onCurrentIndexChanged: _focusFirstItemInCurrentView()
                    
                    // Main OS list
                    ListView {
                        id: oslist
                        model: root.osmodel
                        currentIndex: -1
                        delegate: osdelegate
                        clip: true
                        activeFocusOnTab: true
                        
                        boundsBehavior: Flickable.StopAtBounds
                        highlight: Rectangle { 
                            color: Style.listViewHighlightColor
                            radius: 0 
                        }
                        
                        ScrollBar.vertical: ScrollBar {
                            width: Style.scrollBarWidth
                            policy: oslist.contentHeight > oslist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        }
                        
                        // Keyboard navigation into sublists and selection
                        Keys.onSpacePressed: {
                            if (currentIndex !== -1) {
                                var item = oslist.itemAtIndex(currentIndex)
                                if (item) {
                                    selectOSitem(item.model, true)
                                }
                            }
                        }
                        Keys.onReturnPressed: {
                            if (currentIndex !== -1) {
                                var item = oslist.itemAtIndex(currentIndex)
                                if (item) {
                                    selectOSitem(item.model, true)
                                }
                            }
                        }
                        Keys.onEnterPressed: {
                            if (currentIndex !== -1) {
                                var item = oslist.itemAtIndex(currentIndex)
                                if (item) {
                                    selectOSitem(item.model, true)
                                }
                            }
                        }
                        Keys.onRightPressed: {
                            if (currentIndex !== -1) {
                                var item = oslist.itemAtIndex(currentIndex)
                                if (item && isOSsublist(item.model)) {
                                    selectOSitem(item.model, true)
                                }
                            }
                        }
                        Keys.onUpPressed: {
                            if (currentIndex > 0) currentIndex = currentIndex - 1
                        }
                        Keys.onDownPressed: {
                            if (currentIndex < count - 1) currentIndex = currentIndex + 1
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
                color: (oslist.currentIndex === index) ? Style.listViewHighlightColor :
                       (osMouseArea.containsMouse ? Style.listViewHoverRowBackgroundColor : Style.listViewRowBackgroundColor)
                radius: 0
                
                MouseArea {
                    id: osMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    
                    onClicked: {
                        oslist.currentIndex = index
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
            boundsBehavior: Flickable.StopAtBounds
            highlight: Rectangle { color: Style.listViewHighlightColor; radius: 5 }
            ScrollBar.vertical: ScrollBar {
                width: 10
                policy: sublistview.contentHeight > parent.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }
            activeFocusOnTab: true
            
            // Keyboard selection and navigation
            Keys.onSpacePressed: {
                if (currentIndex !== -1) {
                    selectOSitem(model.get(currentIndex))
                }
            }
            Keys.onReturnPressed: {
                if (currentIndex !== -1) {
                    selectOSitem(model.get(currentIndex))
                }
            }
            Keys.onEnterPressed: {
                if (currentIndex !== -1) {
                    selectOSitem(model.get(currentIndex))
                }
            }
            Keys.onRightPressed: {
                if (currentIndex !== -1 && isOSsublist(model.get(currentIndex))) {
                    selectOSitem(model.get(currentIndex), true)
                }
            }
            Keys.onLeftPressed: {
                osswipeview.decrementCurrentIndex()
                categorySelected = ""
            }
            Keys.onUpPressed: {
                if (currentIndex > 0) currentIndex = currentIndex - 1
            }
            Keys.onDownPressed: {
                if (currentIndex < count - 1) currentIndex = currentIndex + 1
            }
        }
    }

    // OS selection functions (adapted from OSPopup.qml)
    function selectOSitem(model, navigateOnly) {
        console.log("OSSelectionStep: selectOSitem called for", model.name)
        if (navigateOnly === undefined) {
            navigateOnly = false
        }
        
        if (isOSsublist(model)) {
            // Navigate to sublist (whether navigateOnly is true or false)
            console.log("OSSelectionStep: Navigating to sublist:", model.name)
            categorySelected = model.name
            var lm = newSublist()
            populateSublistInto(lm, model)
            // focus first sub item when navigating
            var nextView = osswipeview.itemAt(osswipeview.currentIndex+1)
            nextView.currentIndex = navigateOnly ? 0 : -1
            osswipeview.incrementCurrentIndex()
            // ensure focus is on the new view
            _focusFirstItemInCurrentView()
        } else {
            // Select this OS - explicit branching for clarity
            if (typeof(model.url) === "string" && model.url === "internal://custom") {
                // Use custom: open native file selector now and wait for onFileSelected
                root.wizardContainer.selectedOsName = ""
                root.nextButtonEnabled = false
                root.customSelected = false
                imageWriter.openFileDialog()
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
                console.log("OSSelectionStep: Selected OS", model.name, "- waiting for Next")
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
        
        console.log("OSSelectionStep: DEBUG", model.name)
        console.log("  subitems_json:", JSON.stringify(model.subitems_json), "type:", jsonType, "notEmpty:", jsonNotEmpty, "hasJson:", hasSubitemsJson)
        console.log("  subitems_url:", JSON.stringify(model.subitems_url), "type:", urlType, "notEmpty:", urlNotEmpty, "notBack:", urlNotBack, "hasUrl:", hasSubitemsUrl)
        console.log("  isSublist:", isSublist)
        
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