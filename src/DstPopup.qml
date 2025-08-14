/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.15

import RpiImager

MainPopupBase {
    id: root

    // Provide implementation for the base popup's navigation functions
    function getNextFocusableElement(startElement) {
        var focusableItems = [dstlist, filterSystemDrives, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })

        if (focusableItems.length === 0) return startElement;
        
        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[0];

        var nextIndex = (currentIndex + 1) % focusableItems.length;
        return focusableItems[nextIndex];
    }

    function getPreviousFocusableElement(startElement) {
        var focusableItems = [dstlist, filterSystemDrives, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })

        if (focusableItems.length === 0) return startElement;

        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[focusableItems.length - 1];
        
        var prevIndex = (currentIndex - 1 + focusableItems.length) % focusableItems.length;
        return focusableItems[prevIndex];
    }

    required property ImageWriter imageWriter
    property alias dstlist: dstlist

    // No need to manage polling - it runs continuously in background

    title: qsTr("Storage")

    MainPopupListViewBase {
        id: dstlist
        model: root.imageWriter.getDriveList()
        delegate: dstdelegate
        anchors.top: root.title_separator.bottom
        anchors.right: parent.right

        onActiveFocusChanged: {
            if (activeFocus && currentIndex === -1 && count > 0) {
                currentIndex = 0
            }
        }

        Label {
            anchors.fill: parent
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            visible: parent.count == 0
            text: qsTr("No storage devices found")
            font.bold: true
        }

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && event.modifiers & Qt.ShiftModifier)) {
                root.getPreviousFocusableElement(dstlist).forceActiveFocus()
                event.accepted = true
            } else if (event.key === Qt.Key_Tab) {
                root.getNextFocusableElement(dstlist).forceActiveFocus()
                event.accepted = true
            } else {
                // Allow default up/down arrow processing
                event.accepted = false
            }
        }

        Keys.onSpacePressed: {
            if (currentIndex == -1)
                return
            root.selectDstItem(currentItem)
        }
        Accessible.onPressAction: {
            if (currentIndex == -1)
                return
            root.selectDstItem(currentItem)
        }
    }

    RowLayout {
        id: filterRow
        anchors {
            bottom: parent.bottom
            right: parent.right
            left: parent.left
        }
        Item {
            Layout.fillWidth: true
        }
        ImCheckBox {
            id: filterSystemDrives
            checked: true
            text: qsTr("Exclude System Drives")

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && event.modifiers & Qt.ShiftModifier)) {
                    root.getPreviousFocusableElement(filterSystemDrives).forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab) {
                    root.getNextFocusableElement(filterSystemDrives).forceActiveFocus()
                    event.accepted = true
                }
            }

            onToggled: {
                if (!checked) {
                    // Ask for stern confirmation before disabling filtering
                    confirmUnfilterPopup.open()
                }
            }
        }
    }

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

            anchors.left: parent ? parent.left : undefined
            anchors.right: parent ? parent.right : undefined
            Layout.topMargin: 1
            height: shouldHide ? 0 : 61
            visible: !shouldHide
            Accessible.name: {
                var txt = description+" - "+(size/1000000000).toFixed(1)+ " " + qsTr("gigabytes")
                if (mountpoints.length > 0) {
                    txt += qsTr("Mounted as %1").arg(mountpoints.join(", "))
                }
                return txt;
            }


            Rectangle {
                id: dstbgrect
                anchors.top: parent ? parent.top : undefined
                anchors.left: parent ? parent.left : undefined
                anchors.right: parent ? parent.right : undefined
                height: 60

                color: (dstitem.ListView.view && dstitem.ListView.view.activeFocus && dstitem.ListView.view.currentIndex == index)
                       ? Style.listViewHighlightColor
                       : (mouseOver ? Style.listViewHoverRowBackgroundColor : Style.listViewRowBackgroundColor)
                property bool mouseOver: false

                RowLayout {
                    anchors.fill: parent

                    Item {
                        Layout.preferredWidth: 25
                    }

                    Image {
                        id: dstitem_image
                        source: dstitem.isUsb ? "icons/ic_usb_40px.svg" : dstitem.isScsi ? "icons/ic_storage_40px.svg" : "icons/ic_sd_storage_40px.svg"
                        verticalAlignment: Image.AlignVCenter
                        fillMode: Image.Pad
                    }

                    Item {
                        Layout.preferredWidth: 25
                    }

                    ColumnLayout {
                        Text {
                            textFormat: Text.StyledText
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                            font.family: Style.fontFamily
                            font.pointSize: 16
                            color: !dstitem.unselectable ? "" : Style.formLabelDisabledColor;
                            text: {
                                var sizeStr = (dstitem.size/1000000000).toFixed(1)+ " " + qsTr("GB");
                                return dstitem.description + " - " + sizeStr;
                            }

                        }
                        Text {
                            textFormat: Text.StyledText
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                            font.family: Style.fontFamily
                            font.pointSize: 12
                            color: !dstitem.unselectable ? "" : Style.formLabelDisabledColor;
                            text: {
                                var txt= qsTr("Mounted as %1").arg(dstitem.mountpoints.join(", "));
                                if (dstitem.isReadOnly) {
                                    txt += " " + qsTr("[WRITE PROTECTED]");
                                } else if (dstitem.isSystem) {
                                    txt += " [" + qsTr("SYSTEM") + "]";
                                }
                                return txt;
                            }
                        }
                    }
                }

            }
            Rectangle {
                id: dstborderrect
                anchors.top: dstbgrect.bottom
                anchors.left: parent ? parent.left : undefined
                anchors.right: parent ? parent.right : undefined
                height: 1
                color: Style.popupBorderColor
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: !dstitem.unselectable ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                hoverEnabled: true
                enabled: !dstitem.unselectable

                onEntered: {
                    dstbgrect.mouseOver = true
                }

                onExited: {
                    dstbgrect.mouseOver = false
                }

                onClicked: {
                    root.selectDstItem(dstitem.modelData)
                }
            }
        }
    }

    function selectDstItem(d) {
        if (d.isReadOnly) {
            onError(qsTr("SD card is write protected.<br>Push the lock switch on the left side of the card upwards, and try again."))
            return
        }
        if (d.isSystem) {
            // Show stern confirmation dialog requiring typing the name
            systemDriveConfirm.driveName = d.description
            systemDriveConfirm.device = d.device
            systemDriveConfirm.sizeStr = (d.size/1000000000).toFixed(1) + " " + qsTr("GB")
            systemDriveConfirm.mountpoints = d.mountpoints
            systemDriveConfirm.open()
        } else {
            imageWriter.setDst(d.device)
            window.selectedStorageName = d.description
            if (imageWriter.readyToWrite()) {
                writebutton.enabled = true
            }
                // After a successful selection, ensure filtering is re-enabled
                filterSystemDrives.checked = true
            root.close()
        }
    }

    onOpened: {
        root.contentItem.forceActiveFocus()
    }

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

    ConfirmSystemDrivePopup {
        id: systemDriveConfirm
        onConfirmed: {
            imageWriter.setDst(systemDriveConfirm.device)
            window.selectedStorageName = systemDriveConfirm.driveName
            if (imageWriter.readyToWrite()) {
                writebutton.enabled = true
            }
            // Re-enable filtering after selection via confirmation path
            filterSystemDrives.checked = true
            root.close()
        }
        onCancelled: {
            // no-op
        }
    }
}
