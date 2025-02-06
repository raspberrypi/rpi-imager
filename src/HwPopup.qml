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

    property string hwselected: ""
    property alias deviceModel: deviceModel
    property alias hwlist: hwlist
    required property ListView oslist
    required property SwipeView osswipeview
    title: qsTr("Raspberry Pi Device")

    ListView {
        id: hwlist
        clip: true
        model: ListModel {
            id: deviceModel
            ListElement {
                name: qsTr("[ All ]")
                tags: "[]"
                icon: ""
                description: ""
                matching_type: "exclusive"
            }
        }
        currentIndex: -1
        delegate: hwdelegate
        anchors.top: root.title_separator.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        boundsBehavior: Flickable.StopAtBounds
        highlight: Rectangle { color: "lightsteelblue"; radius: 5 }
        ScrollBar.vertical: ScrollBar {
            anchors.right: parent.right
            width: 10
            policy: hwlist.contentHeight > hwlist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }
        Keys.onSpacePressed: {
            if (currentIndex != -1)
                root.selectHWitem(model.get(currentIndex))
        }
        Accessible.onPressAction: {
            if (currentIndex != -1)
                root.selectHWitem(model.get(currentIndex))
        }
        Keys.onEnterPressed: Keys.onSpacePressed(event)
        Keys.onReturnPressed: Keys.onSpacePressed(event)
    }

    Component {
        id: hwdelegate

        Item {
            width: window.width-100
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
                    selectHWitem(model)
                }
            }

            Rectangle {
                id: bgrect
                anchors.fill: parent
                color: "#f5f5f5"
                visible: mouseOver && parent.ListView.view.currentIndex !== index
                property bool mouseOver: false
            }
            Rectangle {
                id: borderrect
                implicitHeight: 1
                implicitWidth: parent.width
                color: "#dcdcdc"
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
                    source: typeof icon === "undefined" ? "" : icon
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
                        text: name
                        elide: Text.ElideRight
                        font.family: Style.fontFamily
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        font.family: Style.fontFamily
                        text: description
                        wrapMode: Text.WordWrap
                        color: "#1a1a1a"
                    }

                    ToolTip {
                        visible: hwMouseArea.containsMouse && typeof(tooltip) == "string" && tooltip != ""
                        delay: 1000
                        text: typeof(tooltip) == "string" ? tooltip : ""
                        clip: false
                    }
                }
            }
        }
    }

    function selectHWitem(hwmodel) {
        /* Default is exclusive matching */
        var inclusive = false

        if (hwmodel.matching_type) {
            switch (hwmodel.matching_type) {
            case "exclusive":
                break;
            case "inclusive":
                inclusive = true
                break;
            }
        }

        imageWriter.setHWFilterList(hwmodel.tags, inclusive)

        /* Reload list */
        var oslist_json = imageWriter.getFilteredOSlist();
        var o = JSON.parse(oslist_json)
        var oslist_parsed = oslistFromJson(o)
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

        osmodel.clear()
        for (var i in oslist_parsed) {
            osmodel.append(oslist_parsed[i])
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

        hwbutton.text = hwmodel.name
        root.close()
    }
}
