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
    showNextButton: false
    
    property alias oslist: oslist
    property alias osswipeview: osswipeview
    property string categorySelected: ""
    property bool modelLoaded: false
    
    signal updatePopupRequested(var url)
    signal defaultEmbeddedDriveRequested(var drive)
    
    // Forward the nextClicked signal as next() function
    function next() {
        root.nextClicked()
    }
    
    Component.onCompleted: {
        // Connect to osListPrepared signal to reload models when data is ready
        imageWriter.osListPrepared.connect(onOsListPrepared)
        
        // Try initial load in case data is already available
        onOsListPrepared()
    }
    
    // Content
    ColumnLayout {
        anchors.fill: parent
        spacing: 20
        
        // OS selection area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Style.titleBackgroundColor
            border.color: Style.titleSeparatorColor
            border.width: 1
            radius: 8
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15
                
                Text {
                    text: qsTr("Select your operating system:")
                    font.pixelSize: 16
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
                
                // OS SwipeView for navigation between categories
                SwipeView {
                    id: osswipeview
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    interactive: false
                    clip: true
                    
                    // Main OS list
                    ListView {
                        id: oslist
                        model: root.osmodel
                        currentIndex: -1
                        delegate: osdelegate
                        clip: true
                        
                        boundsBehavior: Flickable.StopAtBounds
                        highlight: Rectangle { 
                            color: Style.listViewHighlightColor
                            radius: 5 
                        }
                        
                        ScrollBar.vertical: ScrollBar {
                            width: 10
                            policy: oslist.contentHeight > oslist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        }
                    }
                }
            }
        }
        

    }
    
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
            height: 80
            
            Rectangle {
                id: osbgrect
                anchors.fill: parent
                color: osMouseArea.containsMouse ? Style.listViewHoverRowBackgroundColor : 
                       (oslist.currentIndex === index ? Style.listViewHighlightColor : Style.listViewRowBackgroundColor)
                radius: 5
                
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
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
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
                            radius: 5
                        }
                    }
                    
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        
                        Text {
                            text: delegateItem.name
                            font.pixelSize: 14
                            font.family: Style.fontFamilyBold
                            font.bold: true
                            color: Style.formLabelColor
                            Layout.fillWidth: true
                        }
                        
                        Text {
                            text: delegateItem.description
                            font.pixelSize: 12
                            font.family: Style.fontFamily
                            color: Style.textDescriptionColor
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        
                        Text {
                            text: delegateItem.release_date
                            font.pixelSize: 10
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
            populateSublist(model)
            osswipeview.incrementCurrentIndex()
        } else {
            // Select this OS - use correct setSrc signature from original OSPopup
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
            
            // Store the selected OS name
            root.wizardContainer.selectedOsName = model.name
            
            if (model.subitems_url === "internal://back") {
                osswipeview.decrementCurrentIndex()
                categorySelected = ""
            } else {
                // Auto-advance to next step after selecting an OS
                console.log("OSSelectionStep: Selected OS", model.name, "- auto-advancing to next step")
                root.next()
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
    
    function populateSublist(model) {
        // Implementation would populate sublist - simplified for now
        console.log("Populate sublist for:", model.name)
    }
    

    
    // Called when OS list data is ready from network
    function onOsListPrepared() {
        // Only reload if we haven't loaded yet or if the model is empty
        if (!modelLoaded || root.osmodel.rowCount() === 0) {
            console.log("OSSelectionStep: OS list prepared, reloading OS model")
            
            // Reload OS model only - HW model is already loaded by DeviceSelectionStep
            var osSuccess = root.osmodel.reload()
            
            if (osSuccess) {
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
                            root.imageWriter.startDriveListPolling()
                            root.defaultEmbeddedDriveRequested(imager["embedded_default_destination"])
                        }
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