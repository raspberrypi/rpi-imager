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

    property alias hwlist: hwlist
    required property ListView oslist
    required property SwipeView osswipeview
    required property ListModel osmodel
    required property ImageWriter imageWriter
    readonly property HWListModel deviceModel: imageWriter.getHWList()

    title: qsTr("Raspberry Pi Device")

    MainPopupListViewBase {
        id: hwlist
        anchors.right: parent.right

        model: root.deviceModel
        currentIndex: root.deviceModel.currentIndex
        delegate: hwdelegate
        anchors.top: root.title_separator.bottom

        Keys.onSpacePressed: {
            if (currentIndex != -1)
                root.selectHWitem(currentIndex)
        }
        Accessible.onPressAction: {
            if (currentIndex != -1)
                root.selectHWitem(currentIndex)
        }
    }

    Component {
        id: hwdelegate

        Item {
            id: delegateRoot
            required property int index
            required property string name
            required property string description
            required property string icon
            required property QtObject modelData

            // The code bellow mentions tooltip, but model doesn't have it
            readonly property string tooltip: ""

            width: root.windowWidth - 100
            height: contentLayout.implicitHeight + 24
            Accessible.name: name+".\n"+description

            MouseArea {
                id: hwMouseArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onEntered: {
                    bgrect.mouseOver = true
                }

                onExited: {
                    bgrect.mouseOver = false
                }

                onClicked: {
                    root.selectHWitem(delegateRoot.index)
                }
            }

            Rectangle {
                id: bgrect
                anchors.fill: parent
                color: Style.titleBackgroundColor
                visible: mouseOver && parent.ListView.view.currentIndex !== delegateRoot.index
                property bool mouseOver: false
            }
            Rectangle {
                id: borderrect
                implicitHeight: 1
                implicitWidth: parent.width
                color: Style.popupBorderColor
                y: parent.height
            }

            RowLayout {
                id: contentLayout
                anchors {
                    left: parent.left
                    top: parent.top
                    right: parent.right
                    margins: 12
                }
                spacing: 12

                Image {
                    source: typeof icon === "undefined" ? "" : delegateRoot.icon
                    Layout.preferredHeight: 64
                    Layout.preferredWidth: 64
                    sourceSize.width: 64
                    sourceSize.height: 64
                    fillMode: Image.PreserveAspectFit
                    verticalAlignment: Image.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
                ColumnLayout {
                    Layout.fillWidth: true

                    Text {
                        text: delegateRoot.name
                        elide: Text.ElideRight
                        font.family: Style.fontFamily
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        font.family: Style.fontFamily
                        text: delegateRoot.description
                        wrapMode: Text.WordWrap
                        color: Style.textDescriptionColor
                    }

                    ToolTip {
                        visible: hwMouseArea.containsMouse && typeof(tooltip) == "string" && delegateRoot.tooltip != ""
                        delay: 1000
                        text: typeof(tooltip) == "string" ? delegateRoot.tooltip : ""
                        clip: false
                    }
                }
            }
        }
    }

    function selectHWitem(index) {
        root.deviceModel.setSelectedIndex(index);

        /* Reload list */
        var oslist_json = root.imageWriter.getFilteredOSlist();
        var o = JSON.parse(oslist_json)
        var oslist_parsed = oslistFromJson(o) // qmllint disable unqualified
        if (oslist_parsed === false)
            return

        /* As we're filtering the OS list, we need to ensure we present a 'Recommended' OS.
            * To do this, we exploit a convention of how we build the OS list. By convention,
            * the preferred OS for a device is listed at the top level of the list, and is at the
            * lowest index. So..
            */
        if (oslist_parsed.length != 0) {
            var candidate = oslist_parsed[0]

            if ("description" in candidate &&
                !("subitems" in candidate) &&
                !candidate["description"].includes("(Recommended)")
                )
            {
                candidate["description"] += " (Recommended)"
            }
        }

        root.osmodel.clear()
        for (var i in oslist_parsed) {
            root.osmodel.append(oslist_parsed[i])
        }

        // When the HW device is changed, reset the OS selection otherwise
        // you get a weird effect with the selection moving around in the list
        // when the user next opens the OS list, and the user could still have
        // an OS selected which isn't compatible with this HW device
        oslist.currentIndex = -1
        osswipeview.currentIndex = 0
        imageWriter.setSrc("")
        osbutton.text = qsTr("CHOOSE OS")
        writebutton.enabled = false

        root.close()
    }
}
