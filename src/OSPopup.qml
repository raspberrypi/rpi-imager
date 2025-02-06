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

    property string categorySelected : ""
    property alias oslist: oslist
    property alias osswipeview: osswipeview

    title: qsTr("Operating System")

    onClosed: {
        osswipeview.decrementCurrentIndex()
    }

    SwipeView {
        anchors.top: root.title_separator.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        id: osswipeview
        interactive: false
        clip: true

        MainPopupListViewBase {
            id: oslist
            model: osmodel
            currentIndex: -1
            delegate: osdelegate
            anchors.top: parent.top
            width: root.width

            Keys.onSpacePressed: {
                if (currentIndex != -1)
                    root.selectOSitem(model.get(currentIndex), true)
            }
            Accessible.onPressAction: {
                if (currentIndex != -1)
                    root.selectOSitem(model.get(currentIndex), true)
            }

            Keys.onRightPressed: {
                // Navigate into sublists but don't select an OS entry
                if (currentIndex != -1 && root.isOSsublist(model.get(currentIndex)))
                    root.selectOSitem(model.get(currentIndex), true)
            }
        }
    }

    Component {
        id: suboslist

        ListView {
            model: ListModel {
                ListElement {
                    url: ""
                    icon: "icons/ic_chevron_left_40px.svg"
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
            highlight: Rectangle { color: "lightsteelblue"; radius: 5 }
            ScrollBar.vertical: ScrollBar {
                width: 10
                policy: parent.contentHeight > parent.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }
            Keys.onSpacePressed: {
                if (currentIndex != -1)
                    selectOSitem(model.get(currentIndex))
            }
            Accessible.onPressAction: {
                if (currentIndex != -1)
                    selectOSitem(model.get(currentIndex))
            }
            Keys.onEnterPressed: Keys.onSpacePressed(event)
            Keys.onReturnPressed: Keys.onSpacePressed(event)
            Keys.onRightPressed: {
                // Navigate into sublists but don't select an OS entry
                if (currentIndex != -1 && isOSsublist(model.get(currentIndex)))
                    selectOSitem(model.get(currentIndex), true)
            }
            Keys.onLeftPressed: {
                osswipeview.decrementCurrentIndex()
                ospopup.categorySelected = ""
            }
        }
    }

    Component {
        id: osdelegate

        Item {
            width: window.width-100
            height: contentLayout.implicitHeight + 24
            Accessible.name: name+".\n"+description

            MouseArea {
                id: osMouseArea
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
                    selectOSitem(model)
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
                    source: icon == "icons/ic_build_48px.svg" ? "icons/cat_misc_utility_images.png": icon
                    Layout.preferredHeight: 40
                    Layout.preferredWidth: 40
                    sourceSize.width: 40
                    sourceSize.height: 40
                    fillMode: Image.PreserveAspectFit
                    verticalAlignment: Image.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
                ColumnLayout {
                    Layout.fillWidth: true

                    RowLayout {
                        spacing: 12
                        Text {
                            text: name
                            elide: Text.ElideRight
                            font.family: Style.fontFamily
                            font.bold: true
                        }
                        Image {
                            source: "icons/ic_info_16px.png"
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: 16
                            visible: typeof(website) == "string" && website
                            MouseArea {
                                anchors.fill: parent
                                onClicked: Qt.openUrlExternally(website)
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        font.family: Style.fontFamily
                        text: description
                        wrapMode: Text.WordWrap
                        color: "#1a1a1a"
                    }

                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: "#646464"
                        font.weight: Font.Light
                        visible: typeof(release_date) == "string" && release_date
                        text: qsTr("Released: %1").arg(release_date)
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: "#646464"
                        font.weight: Font.Light
                        visible: typeof(url) == "string" && url != "" && url != "internal://format"
                        text: !url ? "" :
                                     typeof(extract_sha256) != "undefined" && imageWriter.isCached(url,extract_sha256)
                                     ? qsTr("Cached on your computer")
                                     : url.startsWith("file://")
                                       ? qsTr("Local file")
                                       : qsTr("Online - %1 GB download").arg((image_download_size/1073741824).toFixed(1))
                    }

                    ToolTip {
                        visible: osMouseArea.containsMouse && typeof(tooltip) == "string" && tooltip != ""
                        delay: 1000
                        text: typeof(tooltip) == "string" ? tooltip : ""
                        clip: false
                    }
                }
                Image {
                    source: "icons/ic_chevron_right_40px.svg"
                    visible: (typeof(subitems_json) == "string" && subitems_json != "") || (typeof(subitems_url) == "string" && subitems_url != "" && subitems_url != "internal://back")
                    Layout.preferredHeight: 40
                    Layout.preferredWidth: 40
                    fillMode: Image.PreserveAspectFit
                }
            }
        }
    }

    function selectNamedOS(name, collection)
    {
        for (var i = 0; i < collection.count; i++) {
            var os = collection.get(i)

            if (typeof(os.subitems_json) == "string" && os.subitems_json != "") {
                selectNamedOS(name, os.subitems_json)
            }
            else if (typeof(os.url) !== "undefined" && name === os.name) {
                selectOSitem(os, false)
                break
            }
        }
    }

    function selectOSitem(d, selectFirstSubitem)
    {
        if (typeof(d.subitems_json) == "string" && d.subitems_json !== "") {
            var m = newSublist()
            var subitems = JSON.parse(d.subitems_json)

            for (var i in subitems)
            {
                var entry = subitems[i];
                if ("subitems" in entry) {
                    /* Flatten sub-subitems entry */
                    entry["subitems_json"] = JSON.stringify(entry["subitems"])
                    delete entry["subitems"]
                }
                m.append(entry)
            }

            osswipeview.itemAt(osswipeview.currentIndex+1).currentIndex = (selectFirstSubitem === true) ? 0 : -1
            osswipeview.incrementCurrentIndex()
            ospopup.categorySelected = d.name
        } else if (typeof(d.subitems_url) == "string" && d.subitems_url !== "") {
            if (d.subitems_url === "internal://back")
            {
                osswipeview.decrementCurrentIndex()
                ospopup.categorySelected = ""
            }
            else
            {
                console.log("Failure: Backend should have pre-flattened the JSON!");

                osswipeview.itemAt(osswipeview.currentIndex+1).currentIndex = (selectFirstSubitem === true) ? 0 : -1
                osswipeview.incrementCurrentIndex()
            }
        } else if (d.url === "") {
            if (!imageWriter.isEmbeddedMode()) {
                imageWriter.openFileDialog()
            }
            else {
                if (imageWriter.mountUsbSourceMedia()) {
                    var m = newSublist()

                    var usboslist = JSON.parse(imageWriter.getUsbSourceOSlist())
                    for (var i in usboslist) {
                        m.append(usboslist[i])
                    }
                    osswipeview.itemAt(osswipeview.currentIndex+1).currentIndex = (selectFirstSubitem === true) ? 0 : -1
                    osswipeview.incrementCurrentIndex()
                }
                else
                {
                    onError(qsTr("Connect an USB stick containing images first.<br>The images must be located in the root folder of the USB stick."))
                }
            }
        } else {
            imageWriter.setSrc(d.url, d.image_download_size, d.extract_size, typeof(d.extract_sha256) != "undefined" ? d.extract_sha256 : "", typeof(d.contains_multiple_files) != "undefined" ? d.contains_multiple_files : false, ospopup.categorySelected, d.name, typeof(d.init_format) != "undefined" ? d.init_format : "")
            osbutton.text = d.name
            ospopup.close()
            osswipeview.decrementCurrentIndex()
            if (imageWriter.readyToWrite()) {
                writebutton.enabled = true
            }
        }
    }

    function newSublist() {
        if (osswipeview.currentIndex == (osswipeview.count-1))
        {
            var newlist = suboslist.createObject(osswipeview)
            osswipeview.addItem(newlist)
        }

        var m = osswipeview.itemAt(osswipeview.currentIndex+1).model

        if (m.count>1)
        {
            m.remove(1, m.count-1)
        }

        return m
    }

    /// Is the item a sub-list or sub-sub-list in the OS selection model?
    function isOSsublist(d) {
        // Top level category
        if (typeof(d.subitems_json) == "string" && d.subitems_json !== "") {
            return true
        }

        // Sub-category
        if (typeof(d.subitems_url) == "string" && d.subitems_url !== ""
                && d.subitems_url !== "internal://back")
        {
            return true
        }

        return false
    }
}
