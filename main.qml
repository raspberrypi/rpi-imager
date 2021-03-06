/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

import QtQuick 2.9
import QtQuick.Window 2.2
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2

ApplicationWindow {
    id: window
    visible: true

    width: imageWriter.isEmbeddedMode() ? -1 : 680
    height: imageWriter.isEmbeddedMode() ? -1 : 420
    minimumWidth: imageWriter.isEmbeddedMode() ? -1 : 680
    maximumWidth: imageWriter.isEmbeddedMode() ? -1 : 680
    minimumHeight: imageWriter.isEmbeddedMode() ? -1 : 420
    maximumHeight: imageWriter.isEmbeddedMode() ? -1 : 420

    title: qsTr("Raspberry Pi Imager v%1").arg(imageWriter.constantVersion())

    FontLoader {id: roboto;      source: "fonts/Roboto-Regular.ttf"}
    FontLoader {id: robotoLight; source: "fonts/Roboto-Light.ttf"}
    FontLoader {id: robotoBold;  source: "fonts/Roboto-Bold.ttf"}

    onClosing: {
        if (progressBar.visible) {
            close.accepted = false
            quitpopup.openPopup()
        }
    }

    Shortcut {
        sequence: StandardKey.Quit
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!progressBar.visible) {
                Qt.quit()
            }
        }
    }

    Shortcut {
        sequences: ["Shift+Ctrl+X", "Shift+Meta+X"]
        context: Qt.ApplicationShortcut
        onActivated: {
            optionspopup.openPopup()
        }
    }

    ColumnLayout {
        id: bg
        spacing: 0

        Rectangle {
            implicitHeight: window.height/2

            Image {
                id: image
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                fillMode: Image.PreserveAspectFit
                source: imageWriter.isEmbeddedMode() ? "icons/rpi2-hires.png" : "icons/rpi2.png"
                width: window.width
                height: window.height/2
            }
        }

        Rectangle {
            color: "#c31c4a"
            implicitWidth: window.width
            implicitHeight: window.height/2

            GridLayout {
                id: gridLayout
                rowSpacing: 25

                anchors.fill: parent
                anchors.topMargin: 25
                anchors.rightMargin: 50
                anchors.leftMargin: 50

                rows: 3
                columns: 3
                columnSpacing: 25

                ColumnLayout {
                    id: columnLayout
                    spacing: 0
                    Layout.fillWidth: true

                    Text {
                        id: text1
                        color: "#ffffff"
                        text: qsTr("Operating System")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        Layout.preferredWidth: 100
                        font.pixelSize: 12
                        font.family: robotoBold.name
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Button {
                        id: osbutton
                        text: imageWriter.srcFileName() === "" ? qsTr("CHOOSE OS") : imageWriter.srcFileName()
                        font.family: roboto.name
                        spacing: 0
                        padding: 0
                        bottomPadding: 0
                        topPadding: 0
                        Layout.minimumHeight: 40
                        Layout.fillWidth: true
                        onClicked: {
                            ospopup.open()
                            osswipeview.currentItem.forceActiveFocus()
                        }
                        Material.background: "#ffffff"
                        Material.foreground: "#c51a4a"
                        Accessible.ignored: ospopup.visible || dstpopup.visible
                        Accessible.description: qsTr("Select this button to change the operating system")
                        Accessible.onPressAction: clicked()
                    }
                }

                ColumnLayout {
                    id: columnLayout2
                    spacing: 0
                    Layout.fillWidth: true

                    Text {
                        id: text2
                        color: "#ffffff"
                        text: qsTr("Storage")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        Layout.preferredWidth: 100
                        font.pixelSize: 12
                        font.family: robotoBold.name
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Button {
                        id: dstbutton
                        text: qsTr("CHOOSE STORAGE")
                        font.family: roboto.name
                        Layout.minimumHeight: 40
                        Layout.preferredWidth: 100
                        Layout.fillWidth: true
                        onClicked: {
                            imageWriter.startDriveListPolling()
                            dstpopup.open()
                            dstlist.forceActiveFocus()
                        }
                        Material.background: "#ffffff"
                        Material.foreground: "#c51a4a"
                        Accessible.ignored: ospopup.visible || dstpopup.visible
                        Accessible.description: qsTr("Select this button to change the destination storage device")
                        Accessible.onPressAction: clicked()
                    }
                }

                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true

                    Text {
                        text: " "
                        Layout.preferredHeight: 17
                        Layout.preferredWidth: 100
                    }

                    Button {
                        id: writebutton
                        text: qsTr("WRITE")
                        font.family: roboto.name
                        Layout.minimumHeight: 40
                        Layout.fillWidth: true
                        Accessible.ignored: ospopup.visible || dstpopup.visible
                        Accessible.description: qsTr("Select this button to start writing the image")

                        enabled: false
                        Material.background: "#ffffff"
                        Material.foreground: "#c51a4a"
                        onClicked: {
                            if (!imageWriter.readyToWrite()) {
                                return
                            }

                            if (!optionspopup.initialized && imageWriter.hasSavedCustomizationSettings()) {
                                usesavedsettingspopup.openPopup()
                            } else {
                                confirmwritepopup.askForConfirmation()
                            }
                        }
                        Accessible.onPressAction: clicked()
                    }
                }

                ColumnLayout {
                    id: columnLayout3
                    Layout.columnSpan: 3
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter

                    Text {
                        id: progressText
                        font.pointSize: 10
                        color: "white"
                        font.family: robotoBold.name
                        font.bold: true
                        visible: false
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    ProgressBar {
                        id: progressBar
                        Layout.fillWidth: true
                        visible: false
                        Material.background: "#d15d7d"
                    }

                    Button {
                        id: cancelwritebutton
                        text: qsTr("CANCEL WRITE")
                        onClicked: {
                            enabled = false
                            progressText.text = qsTr("Cancelling...")
                            imageWriter.cancelWrite()
                        }
                        Material.background: "#ffffff"
                        Material.foreground: "#c51a4a"
                        Layout.alignment: Qt.AlignRight
                        visible: false
                        font.family: roboto.name
                        Accessible.onPressAction: clicked()
                    }
                    Button {
                        id: cancelverifybutton
                        text: qsTr("CANCEL VERIFY")
                        onClicked: {
                            enabled = false
                            progressText.text = qsTr("Finalizing...")
                            imageWriter.setVerifyEnabled(false)
                        }
                        Material.background: "#ffffff"
                        Material.foreground: "#c51a4a"
                        Layout.alignment: Qt.AlignRight
                        visible: false
                        font.family: roboto.name
                        Accessible.onPressAction: clicked()
                    }
                }
            }
        }
    }

    /*
      Popup for OS selection
     */
    Popup {
        id: ospopup
        x: 50
        y: 25
        width: parent.width-100
        height: parent.height-50
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property string categorySelected : ""

        // background of title
        Rectangle {
            color: "#f5f5f5"
            anchors.right: parent.right
            anchors.top: parent.top
            height: 35
            width: parent.width
        }
        // line under title
        Rectangle {
            color: "#afafaf"
            width: parent.width
            y: 35
            implicitHeight: 1
        }

        Text {
            text: "X"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 25
            anchors.topMargin: 10
            font.family: roboto.name
            font.bold: true

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    ospopup.close()
                }
            }
        }

        ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("Operating System")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                Layout.topMargin: 10
                font.family: roboto.name
                font.bold: true
            }

            Item {
                clip: true
                width: oslist.width
                height: oslist.height

                SwipeView {
                    id: osswipeview
                    interactive: false

                    ListView {
                        id: oslist
                        model: osmodel
                        currentIndex: -1
                        delegate: osdelegate
                        width: window.width-100
                        height: window.height-100
                        boundsBehavior: Flickable.StopAtBounds
                        highlight: Rectangle { color: "lightsteelblue"; radius: 5 }
                        ScrollBar.vertical: ScrollBar {
                            width: 10
                            policy: oslist.contentHeight > oslist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        }
                        Keys.onSpacePressed: {
                            if (currentIndex != -1)
                                selectOSitem(model.get(currentIndex), true)
                        }
                        Accessible.onPressAction: {
                            if (currentIndex != -1)
                                selectOSitem(model.get(currentIndex), true)
                        }
                    }
                }
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
                    subitems: []
                    name: qsTr("Back")
                    description: qsTr("Go back to main menu")
                    tooltip: ""
                }
            }

            currentIndex: -1
            delegate: osdelegate
            width: window.width-100
            height: window.height-100
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
        }
    }

    ListModel {
        id: osmodel

        ListElement {
            url: "internal://format"
            icon: "icons/erase.png"
            extract_size: 0
            image_download_size: 0
            extract_sha256: ""
            contains_multiple_files: false
            release_date: ""
            subitems_url: ""
            subitems: []
            name: qsTr("Erase")
            description: qsTr("Format card as FAT32")
            tooltip: ""
        }

        ListElement {
            url: ""
            icon: "icons/use_custom.png"
            name: qsTr("Use custom")
            description: qsTr("Select a custom .img from your computer")
        }

        Component.onCompleted: {
            if (imageWriter.isOnline()) {
                fetchOSlist();
            }
        }
    }

    Component {
        id: osdelegate

        Item {
            width: window.width-100
            height: image_download_size ? 100 : 60
            Accessible.name: name+".\n"+description

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

            Row {
                leftPadding: 25

                Column {
                    width: 64

                    Image {
                        source: icon == "icons/ic_build_48px.svg" ? "icons/cat_misc_utility_images.png": icon
                        verticalAlignment: Image.AlignVCenter
                        height: parent.parent.parent.height
                        fillMode: Image.Pad
                    }
                    Text {
                        text: " "
//                      visible: !icon
                    }
                }
                Column {
                    width: parent.parent.width-64-50-25

                    Text {
                        verticalAlignment: Text.AlignVCenter
                        height: parent.parent.parent.height
                        font.family: roboto.name
                        textFormat: Text.RichText
                        text: {
                            var txt = "<p style='margin-bottom: 5px;'><b>"+name+"</b></p>"
                            txt += "<font color='#1a1a1a'>"+description+"</font><font style='font-weight: 200' color='#646464'>"
                            if (typeof(release_date) == "string" && release_date)
                                txt += "<br>"+qsTr("Released: %1").arg(release_date)
                            if (typeof(url) == "string" && url != "" && url != "internal://format") {
                                if (typeof(extract_sha256) != "undefined" && imageWriter.isCached(url,extract_sha256)) {
                                    txt += "<br>"+qsTr("Cached on your computer")
                                } else if (url.startsWith("file://")) {
                                    txt += "<br>"+qsTr("Local file")
                                } else {
                                    txt += "<br>"+qsTr("Online - %1 GB download").arg((image_download_size/1073741824).toFixed(1));
                                }
                            }
                            txt += "</font>";

                            return txt;
                        }

                        /*
                        Accessible.role: Accessible.ListItem
                        Accessible.name: name+".\n"+description
                        Accessible.focusable: true
                        Accessible.focused: parent.parent.parent.ListView.view.currentIndex === index
                        */

                        ToolTip {
                            visible: osMouseArea.containsMouse && typeof(tooltip) == "string" && tooltip != ""
                            delay: 1000
                            text: typeof(tooltip) == "string" ? tooltip : ""
                            clip: false
                        }
                    }

                }
                Column {
                    Image {
                        source: "icons/ic_chevron_right_40px.svg"
                        visible: (typeof(subitems) == "object" && subitems.count) || (typeof(subitems_url) == "string" && subitems_url != "" && subitems_url != "internal://back")
                        height: parent.parent.parent.height
                        fillMode: Image.Pad
                    }
                }
            }

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
        }
    }

    /*
      Popup for storage device selection
     */
    Popup {
        id: dstpopup
        x: 50
        y: 25
        width: parent.width-100
        height: parent.height-50
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: imageWriter.stopDriveListPolling()

        // background of title
        Rectangle {
            color: "#f5f5f5"
            anchors.right: parent.right
            anchors.top: parent.top
            height: 35
            width: parent.width
        }
        // line under title
        Rectangle {
            color: "#afafaf"
            width: parent.width
            y: 35
            implicitHeight: 1
        }

        Text {
            text: "X"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 25
            anchors.topMargin: 10
            font.family: roboto.name
            font.bold: true

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    dstpopup.close()
                }
            }
        }

        ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("Storage")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                Layout.topMargin: 10
                font.family: roboto.name
                font.bold: true
            }

            Item {
                clip: true
                width: dstlist.width
                height: dstlist.height

                ListView {
                    id: dstlist
                    model: driveListModel
                    delegate: dstdelegate
                    width: window.width-100
                    height: window.height-100
                    boundsBehavior: Flickable.StopAtBounds
                    highlight: Rectangle { color: "lightsteelblue"; radius: 5 }
                    ScrollBar.vertical: ScrollBar {
                        width: 10
                        policy: dstlist.contentHeight > dstlist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
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
            }
        }
    }

    Component {
        id: dstdelegate

        Item {
            width: window.width-100
            height: 60
            Accessible.name: {
                var txt = description+" - "+(size/1000000000).toFixed(1)+" gigabytes"
                if (mountpoints.length > 0) {
                    txt += qsTr("Mounted as %1").arg(mountpoints.join(", "))
                }
                return txt;
            }
            property string description: model.description
            property string device: model.device
            property string size: model.size

            Rectangle {
               id: dstbgrect
               anchors.fill: parent
               color: "#f5f5f5"
               visible: mouseOver && parent.ListView.view.currentIndex !== index
               property bool mouseOver: false

            }
            Rectangle {
               id: dstborderrect
               implicitHeight: 1
               implicitWidth: parent.width
               color: "#dcdcdc"
               y: parent.height
            }

            Row {
                leftPadding: 25

                Column {
                    width: 64

                    Image {
                        source: isUsb ? "icons/ic_usb_40px.svg" : isScsi ? "icons/ic_storage_40px.svg" : "icons/ic_sd_storage_40px.svg"
                        verticalAlignment: Image.AlignVCenter
                        height: parent.parent.parent.height
                        fillMode: Image.Pad
                    }
                }
                Column {
                    width: parent.parent.width-64

                    Text {
                        textFormat: Text.StyledText
                        height: parent.parent.parent.height
                        verticalAlignment: Text.AlignVCenter
                        font.family: roboto.name
                        text: {
                            var sizeStr = (size/1000000000).toFixed(1)+" GB";
                            var txt;
                            if (isReadOnly) {
                                txt = "<p><font size='4' color='grey'>"+description+" - "+sizeStr+"</font></p>"
                                txt += "<font color='grey'>"
                                if (mountpoints.length > 0) {
                                    txt += qsTr("Mounted as %1").arg(mountpoints.join(", "))+" "
                                }
                                txt += qsTr("[WRITE PROTECTED]")+"</font>"
                            } else {
                                txt = "<p><font size='4'>"+description+" - "+sizeStr+"</font></p>"
                                if (mountpoints.length > 0) {
                                    txt += "<font color='grey'>"+qsTr("Mounted as %1").arg(mountpoints.join(", "))+"</font>"
                                }
                            }
                            return txt;
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

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

    MsgPopup {
        id: msgpopup
    }

    MsgPopup {
        id: quitpopup
        continueButton: false
        yesButton: true
        noButton: true
        title: qsTr("Are you sure you want to quit?")
        text: qsTr("Raspberry Pi Imager is still busy.<br>Are you sure you want to quit?")
        onYes: {
            Qt.quit()
        }
    }

    MsgPopup {
        id: confirmwritepopup
        continueButton: false
        yesButton: true
        noButton: true
        title: qsTr("Warning")
        onYes: {
            writebutton.enabled = false
            cancelwritebutton.enabled = true
            cancelwritebutton.visible = true
            cancelverifybutton.enabled = true
            progressText.text = qsTr("Preparing to write...");
            progressText.visible = true
            progressBar.visible = true
            progressBar.indeterminate = true
            progressBar.Material.accent = "#ffffff"
            osbutton.enabled = false
            dstbutton.enabled = false
            imageWriter.setVerifyEnabled(true)
            imageWriter.startWrite()
        }

        function askForConfirmation()
        {
            text = qsTr("All existing data on '%1' will be erased.<br>Are you sure you want to continue?").arg(dstbutton.text)
            openPopup()
        }
    }

    MsgPopup {
        id: updatepopup
        continueButton: false
        yesButton: true
        noButton: true
        property url url
        title: qsTr("Update available")
        text: qsTr("There is a newer version of Imager available.<br>Would you like to visit the website to download it?")
        onYes: {
            Qt.openUrlExternally(url)
        }
    }

    OptionsPopup {
        id: optionspopup
    }

    UseSavedSettingsPopup {
        id: usesavedsettingspopup
        onYes: {
            optionspopup.initialize()
            optionspopup.applySettings()
            confirmwritepopup.askForConfirmation()
        }
        onNo: {
            imageWriter.clearSavedCustomizationSettings()
            confirmwritepopup.askForConfirmation()
        }
        onEditSettings: {
            optionspopup.openPopup()
        }
    }

    /* Utility functions */
    function httpRequest(url, callback) {
        var xhr = new XMLHttpRequest();
        xhr.timeout = 5000
        xhr.onreadystatechange = (function(x) {
            return function() {
                if (x.readyState === x.DONE)
                {
                    if (x.status === 200)
                    {
                        callback(x)
                    }
                    else
                    {
                        onError(qsTr("Error downloading OS list from Internet"))
                    }
                }
            }
        })(xhr)
        xhr.open("GET", url)
        xhr.send()
    }

    /* Slots for signals imagewrite emits */
    function onDownloadProgress(now,total) {
        var newPos
        if (total) {
            newPos = now/(total+1)
        } else {
            newPos = 0
        }
        if (progressBar.value !== newPos) {
            if (progressText.text === qsTr("Cancelling..."))
                return

            progressText.text = qsTr("Writing... %1%").arg(Math.floor(newPos*100))
            progressBar.indeterminate = false
            progressBar.value = newPos
        }
    }

    function onVerifyProgress(now,total) {
        var newPos
        if (total) {
            newPos = now/total
        } else {
            newPos = 0
        }

        if (progressBar.value !== newPos) {
            if (cancelwritebutton.visible) {
                cancelwritebutton.visible = false
                cancelverifybutton.visible = true
            }

            if (progressText.text === qsTr("Finalizing..."))
                return

            progressText.text = qsTr("Verifying... %1%").arg(Math.floor(newPos*100))
            progressBar.Material.accent = "#6cc04a"
            progressBar.value = newPos
        }
    }

    function onPreparationStatusUpdate(msg) {
        progressText.text = qsTr("Preparing to write... (%1)").arg(msg)
    }

    function resetWriteButton() {
        progressText.visible = false
        progressBar.visible = false
        osbutton.enabled = true
        dstbutton.enabled = true
        writebutton.visible = true
        writebutton.enabled = imageWriter.readyToWrite()
        cancelwritebutton.visible = false
        cancelverifybutton.visible = false
    }

    function onError(msg) {
        msgpopup.title = qsTr("Error")
        msgpopup.text = msg
        msgpopup.openPopup()
        resetWriteButton()
    }

    function onSuccess() {
        msgpopup.title = qsTr("Write Successful")
        if (osbutton.text === qsTr("Erase"))
            msgpopup.text = qsTr("<b>%1</b> has been erased<br><br>You can now remove the SD card from the reader").arg(dstbutton.text)
        else
            msgpopup.text = qsTr("<b>%1</b> has been written to <b>%2</b><br><br>You can now remove the SD card from the reader").arg(osbutton.text).arg(dstbutton.text)
        if (imageWriter.isEmbeddedMode()) {
            msgpopup.continueButton = false
            msgpopup.quitButton = true
        }

        msgpopup.openPopup()
        imageWriter.setDst("")
        dstbutton.text = qsTr("CHOOSE STORAGE")
        resetWriteButton()
    }

    function onFileSelected(file) {
        imageWriter.setSrc(file)
        osbutton.text = imageWriter.srcFileName()
        ospopup.close()
        if (imageWriter.readyToWrite()) {
            writebutton.enabled = true
        }
    }

    function onCancelled() {
        resetWriteButton()
    }

    function onFinalizing() {
        progressText.text = qsTr("Finalizing...")
    }

    function oslistFromJson(o) {
        var lang_country = Qt.locale().name
        if ("os_list_"+lang_country in o) {
            return o["os_list_"+lang_country]
        }
        if (lang_country.includes("_")) {
            var lang = lang_country.substr(0, lang_country.indexOf("_"))
            if ("os_list_"+lang in o) {
                return o["os_list_"+lang]
            }
        }

        if (!"os_list" in o) {
            onError(qsTr("Error parsing os_list.json"))
            return false
        }

        return o["os_list"]
    }

    function fetchOSlist() {
        httpRequest(imageWriter.constantOsListUrl(), function (x) {
            var o = JSON.parse(x.responseText)
            var oslist = oslistFromJson(o)
            if (oslist === false)
                return
            for (var i in oslist) {
                osmodel.insert(osmodel.count-2, oslist[i])
            }

            if ("imager" in o) {
                var imager = o["imager"]
                if ("latest_version" in imager && "url" in imager) {
                    if (!imageWriter.isEmbeddedMode() && imageWriter.isVersionNewer(imager["latest_version"])) {
                        updatepopup.url = imager["url"]
                        updatepopup.openPopup()
                    }
                }
            }
        })
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

    function selectOSitem(d, selectFirstSubitem)
    {
        if (typeof(d.subitems) == "object" && d.subitems.count) {
            var m = newSublist()

            for (var i=0; i<d.subitems.count; i++)
            {
                m.append(d.subitems.get(i))
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
                ospopup.categorySelected = d.name
                var suburl = d.subitems_url
                var m = newSublist()

                httpRequest(suburl, function (x) {
                    var o = JSON.parse(x.responseText)
                    var oslist = oslistFromJson(o)
                    if (oslist === false)
                        return
                    for (var i in oslist) {
                        m.append(oslist[i])
                    }
                })

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

                    var oslist = JSON.parse(imageWriter.getUsbSourceOSlist())
                    for (var i in oslist) {
                        m.append(oslist[i])
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
            imageWriter.setSrc(d.url, d.image_download_size, d.extract_size, typeof(d.extract_sha256) != "undefined" ? d.extract_sha256 : "", typeof(d.contains_multiple_files) != "undefined" ? d.contains_multiple_files : false, ospopup.categorySelected, d.name)
            osbutton.text = d.name
            ospopup.close()
            if (imageWriter.readyToWrite()) {
                writebutton.enabled = true
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
