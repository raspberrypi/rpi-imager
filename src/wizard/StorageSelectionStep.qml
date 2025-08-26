/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("Select your Storage Device")
    showNextButton: true
    
    property alias dstlist: dstlist
    property string selectedDeviceName: ""
    
    // Forward the nextClicked signal as next() function
    function next() {
        root.nextClicked()
    }
    
    // Removed drive list polling calls; backend handles device updates
    
    // Content
    content: [
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // Storage device list fills available space
        ListView {
            id: dstlist
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.imageWriter.getDriveList()
            currentIndex: -1
            delegate: dstdelegate
            clip: true
            
            boundsBehavior: Flickable.StopAtBounds
            highlight: Rectangle { color: Style.listViewHighlightColor; radius: 0 }
            
            ScrollBar.vertical: ScrollBar {
                width: Style.scrollBarWidth
                policy: dstlist.contentHeight > dstlist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }
            
            // No storage devices message
            Label {
                anchors.fill: parent
                horizontalAlignment: Qt.AlignHCenter
                verticalAlignment: Qt.AlignVCenter
                visible: parent.count == 0
                text: qsTr("No storage devices found")
                font.bold: true
                color: Style.formLabelColor
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
                text: qsTr("Exclude System Drives")

                onToggled: {
                    if (!checked) {
                        // If warnings are disabled, bypass the confirmation dialog
                        if (root.imageWriter.getBoolSetting("disable_warnings")) {
                            // Leave checkbox unchecked and continue showing system drives
                            dstlist.forceActiveFocus()
                        } else {
                            // Ask for stern confirmation before disabling filtering
                            confirmUnfilterPopup.open()
                        }
                    }
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
            
            width: dstlist.width
            height: shouldHide ? 0 : 80
            visible: !shouldHide
            
            Rectangle {
                id: dstbgrect
                anchors.fill: parent
                color: (dstlist.currentIndex === dstitem.index) ? Style.listViewHighlightColor :
                       (dstMouseArea.containsMouse ? Style.listViewHoverRowBackgroundColor : Style.listViewRowBackgroundColor)
                radius: 0
                opacity: dstitem.unselectable ? 0.5 : 1.0
                
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
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Style.listItemPadding
                    spacing: Style.spacingMedium
                    
                    // Storage icon
                    Image {
                        source: dstitem.isUsb ? "../icons/ic_usb_40px.svg" :
                                dstitem.isScsi ? "../icons/ic_storage_40px.svg" :
                                "../icons/ic_sd_storage_40px.svg"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        sourceSize.width: 40
                        sourceSize.height: 40
                    }
                    
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.spacingXXSmall
                        
                        Text {
                            text: dstitem.description
                            font.pixelSize: Style.fontSizeFormLabel
                            font.family: Style.fontFamilyBold
                            font.bold: true
                            color: dstitem.unselectable ? Style.formLabelDisabledColor : Style.formLabelColor
                            Layout.fillWidth: true
                        }
                        
                        Text {
                            text: (dstitem.size/1000000000).toFixed(1) + " GB"
                            font.pixelSize: Style.fontSizeDescription
                            font.family: Style.fontFamily
                            color: dstitem.unselectable ? Style.formLabelDisabledColor : Style.textDescriptionColor
                            Layout.fillWidth: true
                        }
                        
                        Text {
                            text: dstitem.mountpoints.length > 0 ? 
                                  qsTr("Mounted as %1").arg(dstitem.mountpoints.join(", ")) : ""
                            font.pixelSize: Style.fontSizeSmall
                            font.family: Style.fontFamily
                            color: dstitem.unselectable ? Style.formLabelDisabledColor : Style.textMetadataColor
                            Layout.fillWidth: true
                            visible: dstitem.mountpoints.length > 0
                        }
                    }
                    
                    // Read-only indicator
                    Text {
                        text: qsTr("Read-only")
                        font.pixelSize: Style.fontSizeDescription
                        font.family: Style.fontFamily
                        color: Style.formLabelErrorColor
                        visible: dstitem.unselectable
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
            systemDriveConfirm.sizeStr = (parseFloat(dstitem.size)/1000000000).toFixed(1) + " " + qsTr("GB")
            systemDriveConfirm.mountpoints = dstitem.mountpoints
            systemDriveConfirm.open()
            return
        }

        imageWriter.setDst(dstitem.device)
        selectedDeviceName = dstitem.description
        root.wizardContainer.selectedStorageName = dstitem.description

        // Do not auto-advance; enable Next
        root.nextButtonEnabled = true
    }

    // Stern confirmation when disabling system drive filtering
    ConfirmUnfilterPopup {
        id: confirmUnfilterPopup
        onConfirmed: {
            // user chose to disable filter; leave checkbox unchecked
        }
        onCancelled: {
            // Re-enable the filter checkbox and keep system drives hidden
            filterSystemDrives.checked = true
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
    ConfirmSystemDrivePopup {
        id: systemDriveConfirm
        onConfirmed: {
            root.imageWriter.setDst(systemDriveConfirm.device)
            root.selectedDeviceName = systemDriveConfirm.driveName
            root.wizardContainer.selectedStorageName = systemDriveConfirm.driveName
            // Re-enable filtering after selection via confirmation path
            filterSystemDrives.checked = true
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