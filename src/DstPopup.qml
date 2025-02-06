/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.15

MainPopupBase {
    id: root

    onClosed: window.imageWriter.stopDriveListPolling()

    property alias dstlist: dstlist

    title: qsTr("Storage")

    MainPopupListViewBase {
        id: dstlist
        model: window.driveListModel
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
            selectDstItem(currentItem)
        }
        Accessible.onPressAction: {
            if (currentIndex == -1)
                return
            selectDstItem(currentItem)
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
            property string description: model.description
            property string device: model.device
            property string size: model.size
            property bool unselectable: (isSystem && filterSystemDrives.checked) || isReadOnly

            Rectangle {
                id: dstbgrect
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 60

                color: mouseOver ? "#f5f5f5" : "#ffffff"
                property bool mouseOver: false

                RowLayout {
                    anchors.fill: parent

                    Item {
                        Layout.preferredWidth: 25
                    }

                    Image {
                        id: dstitem_image
                        source: isUsb ? "icons/ic_usb_40px.svg" : isScsi ? "icons/ic_storage_40px.svg" : "icons/ic_sd_storage_40px.svg"
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
                            color: !dstitem.unselectable ? "" : "grey";
                            text: {
                                var sizeStr = (size/1000000000).toFixed(1)+ " " + qsTr("GB");
                                return description + " - " + sizeStr;
                            }

                        }
                        Text {
                            textFormat: Text.StyledText
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                            font.family: Style.fontFamily
                            font.pointSize: 12
                            color: !dstitem.unselectable ? "" : "grey";
                            text: {
                                var txt= qsTr("Mounted as %1").arg(mountpoints.join(", "));
                                if (isReadOnly) {
                                    txt += " " + qsTr("[WRITE PROTECTED]");
                                } else if (isSystem) {
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
                color: "#dcdcdc"
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
                    selectDstItem(model)
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
