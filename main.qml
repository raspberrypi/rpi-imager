/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

import QtQuick 2.9
import QtQuick.Window 2.2
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
import Qt.labs.settings 1.0

ApplicationWindow {
    id: window
    visible: true
    width: 680
    height: 420
    minimumWidth: 680
    maximumWidth: 680
    minimumHeight: 420
    maximumHeight: 420

    title: qsTr("Raspberry Pi Imager v%1").arg(imageWriter.constantVersion())

    FontLoader {id: roboto;      source: "fonts/Roboto-Regular.ttf"}
    FontLoader {id: robotoLight; source: "fonts/Roboto-Light.ttf"}
    FontLoader {id: robotoBold;  source: "fonts/Roboto-Bold.ttf"}

    ColumnLayout {
        id: bg
        spacing: 0

        Image {
            id: image
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.preferredWidth: window.width
            fillMode: Image.PreserveAspectFit
            source: "icons/rpi2.png"
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
                        onClicked: ospopup.open()
                        Material.background: "#ffffff"
                        Material.foreground: "#c51a4a"
                    }
                }

                ColumnLayout {
                    id: columnLayout2
                    spacing: 0
                    Layout.fillWidth: true

                    Text {
                        id: text2
                        color: "#ffffff"
                        text: qsTr("SD Card")
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
                        text: qsTr("CHOOSE SD CARD")
                        font.family: roboto.name
                        Layout.minimumHeight: 40
                        Layout.preferredWidth: 100
                        Layout.fillWidth: true
                        onClicked: {
                            imageWriter.refreshDriveList()
                            drivePollTimer.start()
                            dstpopup.open()
                        }
                        Material.background: "#ffffff"
                        Material.foreground: "#c51a4a"
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

                        enabled: false
                        Material.background: "#ffffff"
                        Material.foreground: "#c51a4a"
                        onClicked: {
                            if (!imageWriter.readyToWrite())
                                return;
                            enabled = false
                            cancelwritebutton.enabled = true
                            cancelwritebutton.visible = true
                            cancelverifybutton.enabled = true
                            progressText.text = qsTr("Writing... %1%").arg("0")
                            progressText.visible = true
                            progressBar.visible = true
                            progressBar.indeterminate = true
                            progressBar.Material.accent = "#ffffff"
                            osbutton.enabled = false
                            dstbutton.enabled = false
                            imageWriter.setVerifyEnabled(true)
                            imageWriter.startWrite()
                        }
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
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
                        delegate: osdelegate
                        width: window.width-100
                        height: window.height-100
                        focus: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {
                            width: 10
                            policy: oslist.contentHeight > oslist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        }
                    }

                    ListView {
                        id: suboslist
                        model: subosmodel
                        delegate: osdelegate
                        width: window.width-100
                        height: window.height-100
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {
                            width: 10
                            policy: suboslist.contentHeight > suboslist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        }
                    }
                }
            }
        }
    }

    ListModel {
        id: osmodel

        ListElement {
            url: "internal://format"
            icon: "icons/ic_delete_40px.png"
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
            icon: "icons/ic_computer_40px.png"
            name: qsTr("Use custom")
            description: qsTr("Select a custom .img from your computer")
        }

        Component.onCompleted: {
            httpRequest(imageWriter.constantOsListUrl(), function (x) {
                var o = JSON.parse(x.responseText)
                if (!"os_list" in o) {
                    onError(qsTr("Error parsing os_list.json"))
                    return;
                }
                var oslist = o["os_list"]
                for (var i in oslist) {
                    osmodel.insert(osmodel.count-2, oslist[i])
                }
            })
        }
    }

    ListModel {
        id: subosmodel

        ListElement {
            url: ""
            icon: "icons/ic_chevron_left_40px.png"
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

    Component {
        id: osdelegate

        Item {
            width: window.width-100
            height: image_download_size ? 100 : 60

            Rectangle {
               id: bgrect
               anchors.fill: parent
               color: "#f5f5f5"
               visible: false
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
                        source: icon == "icons/ic_build_48px.svg" ? "icons/ic_build_40px.png": icon
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
                                } else {
                                    txt += "<br>"+qsTr("Online - %1 GB download").arg((image_download_size/1073741824).toFixed(1));
                                }
                            }
                            txt += "</font>";

                            return txt;
                        }

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
                        source: "icons/ic_chevron_right_40px.png"
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
                    bgrect.visible = true
                }

                onExited: {
                    bgrect.visible = false
                }

                onClicked: {
                    if (typeof(subitems) == "object" && subitems.count) {
                        if (subosmodel.count>1)
                        {
                            subosmodel.remove(1, subosmodel.count-1)
                        }
                        for (var i=0; i<subitems.count; i++)
                        {
                            subosmodel.append(subitems.get(i))
                        }

                        osswipeview.setCurrentIndex(1)
                    } else if (typeof(subitems_url) == "string" && subitems_url != "") {
                        if (subitems_url == "internal://back")
                        {
                            osswipeview.setCurrentIndex(0)
                        }
                        else
                        {
                            if (subosmodel.count>1)
                            {
                                subosmodel.remove(1, subosmodel.count-1)
                            }

                            httpRequest(subitems_url, function (x) {
                                var o = JSON.parse(x.responseText)
                                if (!"os_list" in o) {
                                    onError(qsTr("Error parsing os_list.json"))
                                    return;
                                }
                                var oslist = o["os_list"]
                                for (var i in oslist) {
                                    subosmodel.append(oslist[i])
                                }
                            })
                            osswipeview.setCurrentIndex(1)
                        }
                    } else if (url == "") {
                        imageWriter.openFileDialog()
                    } else {
                        imageWriter.setSrc(url, image_download_size, extract_size, typeof(extract_sha256) != "undefined" ? extract_sha256 : "", typeof(contains_multiple_files) != "undefined" ? contains_multiple_files : false)
                        osbutton.text = name
                        ospopup.close()
                        if (imageWriter.readyToWrite()) {
                            writebutton.enabled = true
                        }
                    }
                }
            }
        }
    }


    /*
      Popup for SD card device selection
     */
    Popup {
        id: dstpopup
        x: 50
        y: 25
        width: parent.width-100
        height: parent.height-50
        padding: 0
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
                text: qsTr("SD Card")
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
                    focus: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        width: 10
                        policy: dstlist.contentHeight > dstlist.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
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

            Rectangle {
               id: dstbgrect
               anchors.fill: parent
               color: "#f5f5f5"
               visible: false
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
                        source: isUsb ? "icons/ic_usb_40px.png" : isScsi ? "icons/ic_storage_40px.png" : "icons/ic_sd_storage_40px.png"
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
                            var txt = "<p><font size='4'>"+description+" - "+(size/1000000000).toFixed(1)+" GB"+"</font></p>"
                            if (mountpoints.length > 0) {
                                txt += "<font color='grey'>"+qsTr("Mounted as %1").arg(mountpoints.join(", "))+"</font>"
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
                    dstbgrect.visible = true
                }

                onExited: {
                    dstbgrect.visible = false
                }

                onClicked: {
                    drivePollTimer.stop()
                    dstpopup.close()
                    imageWriter.setDst(device, size)
                    dstbutton.text = description
                    if (imageWriter.readyToWrite()) {
                        writebutton.enabled = true
                    }
                }
            }
        }
    }

    Popup {
        id: msgpopup
        x: 75
        y: parent.height/2-100
        width: parent.width-150
        height: msgpopupbody.implicitHeight+150 //200
        padding: 0
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
            id: msgx
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
                    if (continuebutton.visible)
                        msgpopup.close()
                    else
                        Qt.quit()
                }
            }
        }

        ColumnLayout {
            spacing: 20
            anchors.fill: parent

            Text {
                id: msgpopupheader
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                Layout.topMargin: 10
                font.family: roboto.name
                font.bold: true
            }

            Text {
                id: msgpopupbody
                font.pointSize: 12
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                font.family: roboto.name
                Layout.maximumWidth: msgpopup.width-50
                Layout.fillHeight: true
                Layout.leftMargin: 25
                Layout.topMargin: 25
            }

            RowLayout {
                Layout.alignment: Qt.AlignCenter | Qt.AlignBottom
                Layout.bottomMargin: 10
                spacing: 20

                Button {
                    id: quitbutton
                    text: qsTr("QUIT APP")
                    onClicked: Qt.quit()
                    Material.foreground: "#ffffff"
                    Material.background: "#c51a4a"
                    font.family: roboto.name
                    visible: false
                }

                Button {
                    id: continuebutton
                    text: qsTr("CONTINUE")
                    onClicked: msgpopup.close()
                    Material.foreground: "#ffffff"
                    Material.background: "#c51a4a"
                    font.family: roboto.name
                }

                Text { text: " " }
            }
        }
    }

    /* Persistent settings */
    Settings {
        category: "General"
        property alias x: window.x
        property alias y: window.y
    }

    /* Timer for polling drivelist changes */
    Timer {
        id: drivePollTimer
        repeat: true
        onTriggered: imageWriter.refreshDriveList()
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
        msgpopupheader.text = qsTr("Error")
        msgpopupbody.text = msg
        msgpopup.open()
        resetWriteButton()
    }

    function onSuccess() {
        msgpopupheader.text = qsTr("Write Successful")
        if (osbutton.text === qsTr("Erase"))
            msgpopupbody.text = qsTr("<b>%2</b> has been erased<br><br>You can now remove the SD card from the reader").arg(dstbutton.text)
        else
            msgpopupbody.text = qsTr("<b>%1</b> has been written to <b>%2</b><br><br>You can now remove the SD card from the reader").arg(osbutton.text).arg(dstbutton.text)
        msgpopup.open()
        imageWriter.setDst("")
        dstbutton.text = qsTr("CHOOSE SD CARD")
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
}
