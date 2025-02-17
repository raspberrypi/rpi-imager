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

    property string categorySelected : ""
    property alias oslist: oslist
    property alias osswipeview: osswipeview
    property alias osmodel: osmodel

    required property ImageWriter imageWriter

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

    ListModel {
        id: osmodel

        Component.onCompleted: {
            if (root.imageWriter.isOnline()) {
                root.fetchOSlist();
            }
        }
    }

    Component {
        id: suboslist

        ListView {
            id: sublistview
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
            highlight: Rectangle { color: Style.listViewHighlightColor; radius: 5 }
            ScrollBar.vertical: ScrollBar {
                width: 10
                policy: sublistview.contentHeight > parent.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
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
        }
    }

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
            required property int image_download_size // KDAB

            property string website
            property string tooltip
            property string subitems_url

            width: root.windowWidth - 100
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
                    root.selectOSitem(delegateItem.model)
                }
            }

            Rectangle {
                id: bgrect
                anchors.fill: parent
                color: Style.titleBackgroundColor
                visible: mouseOver && parent.ListView.view.currentIndex !== delegateItem.index
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
                    source: delegateItem.icon == "icons/ic_build_48px.svg" ? "icons/cat_misc_utility_images.png": delegateItem.icon
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
                            text: delegateItem.name
                            elide: Text.ElideRight
                            font.family: Style.fontFamily
                            font.bold: true
                        }
                        Image {
                            source: "icons/ic_info_16px.png"
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: 16
                            visible: typeof(website) == "string" && delegateItem.website
                            MouseArea {
                                anchors.fill: parent
                                onClicked: Qt.openUrlExternally(delegateItem.website)
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        font.family: Style.fontFamily
                        text: delegateItem.description
                        wrapMode: Text.WordWrap
                        color: Style.textDescriptionColor
                    }

                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Style.textMetadataColor
                        font.weight: Font.Light
                        visible: typeof(delegateItem.release_date) == "string" && delegateItem.release_date
                        text: qsTr("Released: %1").arg(delegateItem.release_date)
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Style.textMetadataColor
                        font.weight: Font.Light
                        visible: typeof(delegateItem.url) == "string" && delegateItem.url != "" && delegateItem.url != "internal://format"
                        text: !delegateItem.url ? "" :
                                     typeof(delegateItem.extract_sha256) != "undefined" && root.imageWriter.isCached(delegateItem.url, delegateItem.extract_sha256)
                                     ? qsTr("Cached on your computer")
                                     : delegateItem.url.startsWith("file://")
                                       ? qsTr("Local file")
                                       : qsTr("Online - %1 GB download").arg((delegateItem.image_download_size/1073741824).toFixed(1))
                    }

                    ToolTip {
                        visible: osMouseArea.containsMouse && typeof(delegateItem.tooltip) == "string" && delegateItem.tooltip != ""
                        delay: 1000
                        text: typeof(delegateItem.tooltip) == "string" ? delegateItem.tooltip : ""
                        clip: false
                    }
                }
                Image {
                    source: "icons/ic_chevron_right_40px.svg"
                    visible: (typeof(delegateItem.subitems_json) == "string" && delegateItem.subitems_json != "") || (typeof(delegateItem.subitems_url) == "string" && delegateItem.subitems_url != "" && delegateItem.subitems_url != "internal://back")
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
            root.categorySelected = d.name
        } else if (typeof(d.subitems_url) == "string" && d.subitems_url !== "") {
            if (d.subitems_url === "internal://back")
            {
                osswipeview.decrementCurrentIndex()
                root.categorySelected = ""
            }
            else
            {
                console.log("Failure: Backend should have pre-flattened the JSON!");

                osswipeview.itemAt(osswipeview.currentIndex+1).currentIndex = (selectFirstSubitem === true) ? 0 : -1
                osswipeview.incrementCurrentIndex()
            }
        } else if (d.url === "") {
            if (!root.imageWriter.isEmbeddedMode()) {
                root.imageWriter.openFileDialog()
            }
            else {
                if (root.imageWriter.mountUsbSourceMedia()) {
                    var m = newSublist()

                    var usboslist = JSON.parse(root.imageWriter.getUsbSourceOSlist())
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
            root.imageWriter.setSrc(d.url, d.image_download_size, d.extract_size, typeof(d.extract_sha256) != "undefined" ? d.extract_sha256 : "", typeof(d.contains_multiple_files) != "undefined" ? d.contains_multiple_files : false, root.categorySelected, d.name, typeof(d.init_format) != "undefined" ? d.init_format : "")
            osbutton.text = d.name
            root.close()
            osswipeview.decrementCurrentIndex()
            if (root.imageWriter.readyToWrite()) {
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

        var m = osswipeview.itemAt(osswipeview.currentIndex+1).model // qmllint disable missing-property

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

    function fetchOSlist() {
        var oslist_json = root.imageWriter.getFilteredOSlist();
        var o = JSON.parse(oslist_json)
        var oslist_parsed = oslistFromJson(o) // qmllint disable unqualified
        if (oslist_parsed === false)
            return
        osmodel.clear()
        for (var i in oslist_parsed) {
            osmodel.append(oslist_parsed[i])
        }

        if ("imager" in o) {
            var imager = o["imager"]

            if ("devices" in imager)
            {
                hwpopup.deviceModel.clear()
                var devices = imager["devices"]
                for (var j in devices)
                {
                    devices[j]["tags"] = JSON.stringify(devices[j]["tags"])
                    hwpopup.deviceModel.append(devices[j])
                    if ("default" in devices[j] && devices[j]["default"])
                    {
                        hwpopup.hwlist.currentIndex = hwpopup.deviceModel.count-1
                    }
                }
            }

            if (root.imageWriter.getBoolSetting("check_version") && "latest_version" in imager && "url" in imager) {
                if (!root.imageWriter.isEmbeddedMode() && root.imageWriter.isVersionNewer(imager["latest_version"])) {
                    updatepopup.url = imager["url"]
                    updatepopup.openPopup()
                }
            }
            if ("default_os" in imager) {
                selectNamedOS(imager["default_os"], osmodel)
            }
            if (root.imageWriter.isEmbeddedMode()) {
                if ("embedded_default_os" in imager) {
                    selectNamedOS(imager["embedded_default_os"], osmodel)
                }
                if ("embedded_default_destination" in imager) {
                    root.imageWriter.startDriveListPolling()
                    setDefaultDest.drive = imager["embedded_default_destination"]
                    setDefaultDest.start()
                }
            }
        }
    }
}
