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

    required property ImageWriter imageWriter
    property alias dstlist: dstlist

    onClosed: imageWriter.stopDriveListPolling()

    title: qsTr("Storage")

    MainPopupListViewBase {
        id: dstlist
        model: root.imageWriter.getDriveList()
        delegate: dstdelegate
        anchors.top: root.title_separator.bottom
        anchors.right: parent.right

        Label {
            anchors.fill: parent
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            visible: parent.count == 0
            text: qsTr("No storage devices found")
            font.bold: true
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
        }
    }

    Component {
        id: dstdelegate

        Item {
            id: dstitem

            required property string device
            required property string description
            required property string size
            required property bool isUsb
            required property bool isScsi
            required property bool isReadOnly
            required property bool isSystem
            required property var mountpoints
            required property QtObject modelData

            readonly property bool unselectable: (isSystem && filterSystemDrives.checked) || isReadOnly

            anchors.left: parent.left
            anchors.right: parent.right
            Layout.topMargin: 1
            height: 61
            Accessible.name: {
                var txt = description+" - "+(size/1000000000).toFixed(1)+ " " + qsTr("gigabytes")
                if (mountpoints.length > 0) {
                    txt += qsTr("Mounted as %1").arg(mountpoints.join(", "))
                }
                return txt;
            }


            Rectangle {
                id: dstbgrect
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 60

                color: mouseOver ? Style.listViewHoverRowBackgroundColor : Style.listViewRowBackgroundColor
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
                                    text += " [" + qsTr("SYSTEM") + "]";
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
                anchors.left: parent.left
                anchors.right: parent.right
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

        dstpopup.close()
        imageWriter.setDst(d.device, d.size)
        dstbutton.text = d.description
        if (imageWriter.readyToWrite()) {
            writebutton.enabled = true
        }
    }
}
