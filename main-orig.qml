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
    /*minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    minimumHeight: gridLayout.height+100*/
    minimumWidth: 680
    maximumWidth: 680
    minimumHeight: 420
    maximumHeight: 420

    title: qsTr("Raspberry Pi Imaging Utility")

    GridLayout {
        id: gridLayout
        rowSpacing: 25
        anchors.top: parent.top
        anchors.topMargin: 50
        anchors.right: parent.right
        anchors.rightMargin: 50
        anchors.left: parent.left
        anchors.leftMargin: 50
        rows: 4
        columns: 3

        Image {
            id: image
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.columnSpan: 3
            Layout.preferredHeight: 92
            Layout.preferredWidth: 330
            fillMode: Image.PreserveAspectFit
            source: "icons/rpi.png"
        }

        ColumnLayout {
            id: columnLayout
            spacing: 0
            Layout.fillWidth: true

            Text {
                id: text1
                color: "#696969"
                text: qsTr("Operating System")
                Layout.fillWidth: true
                Layout.preferredHeight: 17
                Layout.preferredWidth: 132
                font.pixelSize: 12
            }

            Button {
                id: osbutton
                text: imageWriter.srcFileName() === "" ? qsTr("CHOOSE OS") : imageWriter.srcFileName()
                spacing: 0
                padding: 0
                bottomPadding: 0
                topPadding: 0
                Layout.minimumHeight: 40
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                onClicked: {
                    ospopup.open()
                    // force property bindings to refresh
                    window.setHeight(window.height)
                }
                Material.background: "#ffffff"
                Material.foreground: "#c51a4a"
            }
        }

        Item {
            id: spacer1
            Layout.preferredHeight: 10
            Layout.preferredWidth: 25
        }

        ColumnLayout {
            id: columnLayout2
            spacing: 0
            Layout.fillWidth: true

            Text {
                id: text2
                color: "#696969"
                text: qsTr("SD Card")
                Layout.fillWidth: true
                Layout.preferredHeight: 17
                Layout.preferredWidth: 127
                font.pixelSize: 12
            }

            Button {
                id: dstbutton
                text: qsTr("CHOOSE SD")
                Layout.minimumHeight: 40
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


        Switch {
            Layout.columnSpan: 3
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumHeight: 25

            id: verifySwitch
            text: qsTr("Verify written data")
            checked: true
            onCheckedChanged: {
                imageWriter.setVerifyEnabled(verifySwitch.checked)
            }
        }

        ColumnLayout {
            id: columnLayout3
            Layout.columnSpan: 3
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter

            Button {
                id: writebutton
                text: qsTr("WRITE")
                font.pointSize: 11
                Layout.minimumWidth: 100
                Layout.minimumHeight: 50
                topPadding: 12
                enabled: false
                Material.foreground: "#ffffff"
                Material.background: "#c51a4a"
                onClicked: {
                    if (!imageWriter.readyToWrite())
                        return;
                    visible = false
                    cancelbutton.enabled = true
                    cancelbutton.visible = true
                    progressText.visible = true
                    progressBar.visible = true
                    progressBar.indeterminate = true
                    progressBar.Material.accent = Material.Pink
                    osbutton.enabled = false
                    dstbutton.enabled = false
                    imageWriter.setVerifyEnabled(verifySwitch.checked)
                    imageWriter.startWrite()
                }
            }

            Text {
                id: progressText
                font.pointSize: 10
                color: "grey"
                text: qsTr("Writing... %1%").arg("0")
                visible: false
            }

            ProgressBar {
                id: progressBar
                Layout.fillWidth: true
                visible: false
            }

            Button {
                id: cancelbutton
                text: qsTr("CANCEL")
                onClicked: {
                    enabled = false
                    imageWriter.cancelWrite()
                }
                Material.background: "#ffffff"
                Material.foreground: "#c51a4a"
                Layout.alignment: Qt.AlignRight
                visible: false
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
        padding: 25
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("Operating System")
                font.pointSize: 14
            }
            Rectangle {
                id: hr
                implicitWidth: parent.width //540
                implicitHeight: 1
                color: "#1E000000"
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
                        width: window.width-140 //540
                        height: window.height-170 //250
                        focus: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {
                            width: 10
                            policy: ScrollBar.AlwaysOn
                        }
                    }

                    ListView {
                        id: suboslist
                        model: subosmodel
                        delegate: osdelegate
                        width: window.width-140 //540
                        height: window.height-170 //250
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {
                            width: 10
                            policy: ScrollBar.AlwaysOn
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
            icon: "icons/ic_delete_48px.svg"
            extract_size: 0
            image_download_size: 0
            extract_sha256: ""
            contains_multiple_files: false
            release_date: ""
            subitems_url: ""
            subitems: []
            name: qsTr("Erase")
            description: qsTr("Format card as FAT32")
        }

        ListElement {
            url: ""
            icon: "icons/ic_computer_48px.svg"
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
            icon: "icons/ic_chevron_left_48px.svg"
            extract_size: 0
            image_download_size: 0
            extract_sha256: ""
            contains_multiple_files: false
            release_date: ""
            subitems_url: "internal://back"
            subitems: []
            name: qsTr("Back")
            description: qsTr("Go back to main menu")
        }
    }

    Component {
        id: osdelegate

        Item {
            width: window.width-140 //540
            height: image_download_size ? 100 : 60

            Rectangle {
               id: bgrect
               anchors.fill: parent
               color: "#f5f5f5"
               visible: false
            }

            Row {
                Column {
                    width: 64

                    Image {
                        source: icon
                        verticalAlignment: Image.AlignVCenter
                        height: parent.parent.parent.height
                        fillMode: Image.Pad
                    }
                    Text {
                        text: " "
                        visible: !icon
                    }
                }
                Column {
                    width: parent.parent.width-64-50
                    //height: parent.parent.height

                    Text {
                        verticalAlignment: Text.AlignVCenter
                        height: parent.parent.parent.height
                        textFormat: Text.StyledText
                        text: {
                            var txt = "<font size='4'>"+name+"</font><br>"
                            txt += "<font color='grey'>"+description
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
                    }

                }
                Column {
                    Image {
                        source: "icons/ic_chevron_right_48px.svg"
                        visible: (typeof(subitems) == "object" && subitems.count) || (typeof(subitems_url) == "string" && subitems_url != "" && subitems_url != "internal://back")
                        height: parent.parent.parent.height
                        fillMode: Image.Pad
                    }
                }
            }

            MouseArea {
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
                        //fileDialog.open()
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
        padding: 25
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        ColumnLayout {
            spacing: 10

            Text {
                text: qsTr("SD Card")
                font.pointSize: 14
            }
            Text {
                text: qsTr("Any existing data on the device selected will be DELETED")
                font.pointSize: 10
                color: "#c51a4a"
            }
            Rectangle {
                implicitWidth: parent.width //540
                implicitHeight: 1
                color: "#1E000000"
            }

            Item {
                clip: true
                width: dstlist.width
                height: dstlist.height

                ListView {
                    id: dstlist
                    model: driveListModel
                    delegate: dstdelegate
                    width: window.width-140 //540
                    height: window.height-170 //250
                    focus: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        width: 10
                    }
                }
            }
        }
    }

    Component {
        id: dstdelegate

        Item {
            width: window.width-140 //540
            height: 50

            Rectangle {
               id: dstbgrect
               anchors.fill: parent
               color: "#f5f5f5"
               visible: false
            }

            Row {
                Column {
                    width: 64

                    Image {
                        source: isUsb ? "icons/ic_usb_48px.svg" : isScsi ? "icons/ic_storage_48px.svg" : "icons/ic_sd_storage_48px.svg"
                    }
                }
                Column {
                    width: parent.parent.width-64

                    Text {
                        textFormat: Text.StyledText
                        text: {
                            var txt = "<font size='4'>"+description+" - "+Math.floor(size/1000000000)+" GB"+"</font><br>"
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
        y: 50
        width: parent.width-150
        height: parent.height-100
        padding: 25
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        ColumnLayout {
            spacing: 10
            anchors.fill: parent

            Text {
                id: msgpopupheader
                font.pointSize: 14
            }

            Text {
                id: msgpopupbody
                font.pointSize: 12
                wrapMode: Text.Wrap
                Layout.maximumWidth: msgpopup.width-50 //msgpopup.width-20
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight | Qt.AlignBottom
                spacing: 20

                Button {
                    id: quitbutton
                    text: qsTr("QUIT APP")
                    onClicked: Qt.quit()
                    Material.background: "#ffffff"
                    Material.foreground: "#c51a4a"
                }

                Button {
                    id: continuebutton
                    text: qsTr("CONTINUE")
                    onClicked: msgpopup.close()
                    Material.background: "#ffffff"
                    Material.foreground: "#6cc04a"
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
        property alias verifyEnabled: verifySwitch.checked
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
        if (progressBar.value != newPos) {
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

        if (progressBar.value != newPos) {
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
        cancelbutton.visible = false
    }

    function onError(msg) {
        msgpopupheader.text = qsTr("Error")
        msgpopupbody.text = msg
        msgpopup.open()
        resetWriteButton()
    }

    function onSuccess() {
        msgpopupheader.text = qsTr("Write Successful")
        msgpopupbody.text = osbutton.text+" "+qsTr("has been written to")+" "+dstbutton.text+"\r\n"+qsTr("You can now remove card from reader");
        continuebutton.visible = false
        msgpopup.open()
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
}
