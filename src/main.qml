/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Window 2.2
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
import "qmlcomponents"

ApplicationWindow {
    id: window
    visible: true

    width: imageWriter.isEmbeddedMode() ? -1 : 680
    height: imageWriter.isEmbeddedMode() ? -1 : 450
    minimumWidth: imageWriter.isEmbeddedMode() ? -1 : 680
    minimumHeight: imageWriter.isEmbeddedMode() ? -1 : 420

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
            id: logoContainer
            implicitHeight: window.height/4

            Image {
                id: image
                source: "icons/logo_sxs_imager.png"

                // Specify the maximum size of the image
                width: window.width * 0.45
                height: window.height / 3

                // Within the image's specified size rectangle, resize the
                // image to fit within the rectangle while keeping its aspect
                // ratio the same.  Preserving the aspect ratio implies some
                // extra padding between the Image's extend and the actual
                // image content: align left so all this padding is on the
                // right.
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft

                // Keep the left side of the image 40 pixels from the left
                // edge
                anchors.left: logoContainer.left
                anchors.leftMargin: 40

                // Equal padding above and below the image
                anchors.top: logoContainer.top
                anchors.bottom: logoContainer.bottom
                anchors.topMargin: window.height / 25
                anchors.bottomMargin: window.height / 25
            }
        }

        Rectangle {
            color: "#cd2355"
            implicitWidth: window.width
            implicitHeight: window.height * (1 - 1/4)

            GridLayout {
                id: gridLayout
                rowSpacing: 15

                anchors.fill: parent
                anchors.topMargin: 25
                anchors.rightMargin: 50
                anchors.leftMargin: 50

                rows: 5
                columns: 3
                columnSpacing: 15

                ColumnLayout {
                    id: columnLayout0
                    spacing: 0
                    Layout.row: 0
                    Layout.column: 0
                    Layout.fillWidth: true

                    Text {
                        id: text0
                        color: "#ffffff"
                        text: qsTr("Raspberry Pi Device")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        Layout.preferredWidth: 100
                        font.pixelSize: 12
                        font.family: robotoBold.name
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ImButton {
                        id: hwbutton
                        text: qsTr("CHOOSE DEVICE")
                        spacing: 0
                        padding: 0
                        bottomPadding: 0
                        topPadding: 0
                        Layout.minimumHeight: 40
                        Layout.fillWidth: true
                        onClicked: {
                            hwpopup.open()
                            hwlist.forceActiveFocus()
                        }
                        Accessible.ignored: ospopup.visible || dstpopup.visible || hwpopup.visible
                        Accessible.description: qsTr("Select this button to choose your target Raspberry Pi")
                    }
                }

                ColumnLayout {
                    id: columnLayout1
                    spacing: 0
                    Layout.row: 0
                    Layout.column: 1
                    Layout.fillWidth: true

                    Text {
                        id: text1
                        color: "#ffffff"
                        text: qsTr("Operating System")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        font.pixelSize: 12
                        font.family: robotoBold.name
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ImButton {
                        id: osbutton
                        text: imageWriter.srcFileName() === "" ? qsTr("CHOOSE OS") : imageWriter.srcFileName()
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
                        Accessible.ignored: ospopup.visible || dstpopup.visible || hwpopup.visible
                        Accessible.description: qsTr("Select this button to change the operating system")
                    }
                }

                ColumnLayout {
                    id: columnLayout2
                    spacing: 0
                    Layout.row: 0
                    Layout.column: 2
                    Layout.fillWidth: true

                    Text {
                        id: text2
                        color: "#ffffff"
                        text: qsTr("Storage")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        font.pixelSize: 12
                        font.family: robotoBold.name
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ImButton {
                        id: dstbutton
                        text: qsTr("CHOOSE STORAGE")
                        spacing: 0
                        padding: 0
                        bottomPadding: 0
                        topPadding: 0
                        Layout.minimumHeight: 40
                        Layout.preferredWidth: 200
                        Layout.fillWidth: true
                        onClicked: {
                            imageWriter.startDriveListPolling()
                            dstpopup.open()
                            dstlist.forceActiveFocus()
                        }
                        Accessible.ignored: ospopup.visible || dstpopup.visible || hwpopup.visible
                        Accessible.description: qsTr("Select this button to change the destination storage device")
                    }
                }

                ColumnLayout {
                    id: columnLayoutProgress
                    spacing: 0
                    Layout.row: 1
                    Layout.column: 0
                    Layout.columnSpan: 2

                    Text {
                        id: progressText
                        font.pointSize: 10
                        color: "white"
                        font.family: robotoBold.name
                        font.bold: true
                        visible: false
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        Layout.bottomMargin: 25
                    }

                    ProgressBar {
                        Layout.bottomMargin: 25
                        id: progressBar
                        Layout.fillWidth: true
                        visible: false
                        Material.background: "#d15d7d"
                    }
                }

                ColumnLayout {
                    id: columnLayout3
                    Layout.row: 1
                    Layout.column: 2
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    spacing: 0

                    ImButton {
                        Layout.bottomMargin: 25
                        Layout.minimumHeight: 40
                        Layout.preferredWidth: 200
                        padding: 5
                        id: cancelwritebutton
                        text: qsTr("CANCEL WRITE")
                        onClicked: {
                            enabled = false
                            progressText.text = qsTr("Cancelling...")
                            imageWriter.cancelWrite()
                        }
                        Layout.alignment: Qt.AlignRight
                        visible: false
                    }
                    ImButton {
                        Layout.bottomMargin: 25
                        Layout.minimumHeight: 40
                        Layout.preferredWidth: 200
                        padding: 5
                        id: cancelverifybutton
                        text: qsTr("CANCEL VERIFY")
                        onClicked: {
                            enabled = false
                            progressText.text = qsTr("Finalizing...")
                            imageWriter.setVerifyEnabled(false)
                        }
                        Layout.alignment: Qt.AlignRight
                        visible: false
                    }

                    ImButton {
                        id: writebutton
                        text: qsTr("Next")
                        Layout.bottomMargin: 25
                        Layout.minimumHeight: 40
                        Layout.preferredWidth: 200
                        Layout.alignment: Qt.AlignRight
                        Accessible.ignored: ospopup.visible || dstpopup.visible || hwpopup.visible
                        Accessible.description: qsTr("Select this button to start writing the image")
                        enabled: false
                        onClicked: {
                            if (!imageWriter.readyToWrite()) {
                                return
                            }

                            if (!optionspopup.visible && imageWriter.imageSupportsCustomization()) {
                                usesavedsettingspopup.openPopup()
                            } else {
                                confirmwritepopup.askForConfirmation()
                            }
                        }
                    }
                }

                Text {
                    Layout.columnSpan: 3
                    color: "#ffffff"
                    font.pixelSize: 18
                    font.family: roboto.name
                    visible: imageWriter.isEmbeddedMode() && imageWriter.customRepo()
                    text: qsTr("Using custom repository: %1").arg(imageWriter.constantOsListUrl())
                }

                Text {
                    id: networkInfo
                    Layout.columnSpan: 3
                    color: "#ffffff"
                    font.pixelSize: 18
                    font.family: roboto.name
                    visible: imageWriter.isEmbeddedMode()
                    text: qsTr("Network not ready yet")
                }

                Text {
                    Layout.columnSpan: 3
                    color: "#ffffff"
                    font.pixelSize: 18
                    font.family: roboto.name
                    visible: !imageWriter.hasMouse()
                    text: qsTr("Keyboard navigation: <tab> navigate to next button <space> press button/select item <arrow up/down> go up/down in lists")
                }

                Rectangle {
                    id: langbarRect
                    Layout.columnSpan: 3
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                    Layout.bottomMargin: 5
                    visible: imageWriter.isEmbeddedMode()
                    implicitWidth: langbar.width
                    implicitHeight: langbar.height
                    color: "#ffffe3"
                    radius: 5

                    RowLayout {
                        id: langbar
                        spacing: 10

                        Text {
                            font.pixelSize: 12
                            font.family: roboto.name
                            text: qsTr("Language: ")
                            Layout.leftMargin: 30
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                        }
                        ComboBox {
                            font.pixelSize: 12
                            font.family: roboto.name
                            model: imageWriter.getTranslations()
                            Layout.preferredWidth: 200
                            currentIndex: -1
                            Component.onCompleted: {
                                currentIndex = find(imageWriter.getCurrentLanguage())
                            }
                            onActivated: {
                                imageWriter.changeLanguage(editText)
                            }
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                        }
                        Text {
                            font.pixelSize: 12
                            font.family: roboto.name
                            text: qsTr("Keyboard: ")
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                        }
                        ComboBox {
                            enabled: imageWriter.isEmbeddedMode()
                            font.pixelSize: 12
                            font.family: roboto.name
                            model: imageWriter.getKeymapLayoutList()
                            currentIndex: -1
                            Component.onCompleted: {
                                currentIndex = find(imageWriter.getCurrentKeyboard())
                            }
                            onActivated: {
                                imageWriter.changeKeyboard(editText)
                            }
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                            Layout.rightMargin: 30
                        }
                    }
                }

                /* Language/keyboard bar is normally only visible in embedded mode.
                   To test translations also show it when shift+ctrl+L is pressed. */
                Shortcut {
                    sequences: ["Shift+Ctrl+L", "Shift+Meta+L"]
                    context: Qt.ApplicationShortcut
                    onActivated: {
                        langbarRect.visible = true
                    }
                }
            }

            DropArea {
                anchors.fill: parent
                onEntered: {
                    if (drag.active && mimeData.hasUrls()) {
                        drag.acceptProposedAction()
                    }
                }
                onDropped: {
                    if (drop.urls && drop.urls.length > 0) {
                        onFileSelected(drop.urls[0].toString())
                    }
                }
            }
        }
    }

    Popup {
        id: hwpopup
        x: 50
        y: 25
        width: parent.width-100
        height: parent.height-50
        padding: 0
        closePolicy: Popup.CloseOnEscape
        property string hwselected: ""

        // background of title
        Rectangle {
            id: hwpopup_title_background
            color: "#f5f5f5"
            anchors.left: parent.left
            anchors.top: parent.top
            height: 35
            width: parent.width

            Text {
                text: qsTr("Raspberry Pi Device")
                horizontalAlignment: Text.AlignHCenter
                anchors.fill: parent
                anchors.topMargin: 10
                font.family: roboto.name
                font.bold: true
            }

            Text {
                text: "X"
                Layout.alignment: Qt.AlignRight
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
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
                        hwpopup.close()
                    }
                }
            }
        }
        // line under title
        Rectangle {
            id: hwpopup_title_separator
            color: "#afafaf"
            width: parent.width
            anchors.top: hwpopup_title_background.bottom
            height: 1
        }

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
            anchors.top: hwpopup_title_separator.bottom
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
                    selectHWitem(model.get(currentIndex))
            }
            Accessible.onPressAction: {
                if (currentIndex != -1)
                    selectHWitem(model.get(currentIndex))
            }
            Keys.onEnterPressed: Keys.onSpacePressed(event)
            Keys.onReturnPressed: Keys.onSpacePressed(event)
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
        closePolicy: Popup.CloseOnEscape
        property string categorySelected : ""

        // background of title
        Rectangle {
            id: ospopup_title_background
            color: "#f5f5f5"
            anchors.left: parent.left
            anchors.top: parent.top
            height: 35
            width: parent.width

            Text {
                text: qsTr("Operating System")
                horizontalAlignment: Text.AlignHCenter
                anchors.fill: parent
                anchors.topMargin: 10
                font.family: roboto.name
                font.bold: true
            }

            Text {
                text: "X"
                Layout.alignment: Qt.AlignRight
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
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
                        osswipeview.decrementCurrentIndex()
                    }
                }
            }
        }
        // line under title
        Rectangle {
            id: ospopup_title_separator
            color: "#afafaf"
            width: parent.width
            anchors.top: ospopup_title_background.bottom
            height: 1
        }

        SwipeView {
            anchors.top: ospopup_title_separator.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            id: osswipeview
            interactive: false
            clip: true

            ListView {
                id: oslist
                model: osmodel
                currentIndex: -1
                delegate: osdelegate
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: ospopup.width
                boundsBehavior: Flickable.StopAtBounds
                highlight: Rectangle { color: "lightsteelblue"; radius: 5 }
                ScrollBar.vertical: ScrollBar {
                    anchors.right: parent.right
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
                Keys.onEnterPressed: Keys.onSpacePressed(event)
                Keys.onReturnPressed: Keys.onSpacePressed(event)
                Keys.onRightPressed: {
                    // Navigate into sublists but don't select an OS entry
                    if (currentIndex != -1 && isOSsublist(model.get(currentIndex)))
                        selectOSitem(model.get(currentIndex), true)
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

    ListModel {
        id: osmodel

        Component.onCompleted: {
            if (imageWriter.isOnline()) {
                fetchOSlist();
            }
        }
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
                        font.family: roboto.name
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        font.family: roboto.name
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
                            font.family: roboto.name
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
                        font.family: roboto.name
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
        closePolicy: Popup.CloseOnEscape
        onClosed: imageWriter.stopDriveListPolling()

        // background of title
        Rectangle {
            id: dstpopup_title_background
            color: "#f5f5f5"
            anchors.left: parent.left
            anchors.top: parent.top
            height: 35
            width: parent.width

            Text {
                text: qsTr("Storage")
                horizontalAlignment: Text.AlignHCenter
                anchors.fill: parent
                anchors.topMargin: 10
                font.family: roboto.name
                font.bold: true
            }

            Text {
                text: "X"
                Layout.alignment: Qt.AlignRight
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
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
        }
        // line under title
        Rectangle {
            id: dstpopup_title_separator
            color: "#afafaf"
            width: parent.width
            anchors.top: dstpopup_title_background.bottom
            height: 1
        }
        ListView {
            id: dstlist
            model: driveListModel
            delegate: dstdelegate

            anchors.top: dstpopup_title_separator.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            boundsBehavior: Flickable.StopAtBounds
            highlight: Rectangle { color: "lightsteelblue"; radius: 5 }
            clip: true

            Label {
                anchors.fill: parent
                horizontalAlignment: Qt.AlignHCenter
                verticalAlignment: Qt.AlignVCenter
                visible: parent.count == 0
                text: qsTr("No storage devices found")
                font.bold: true
            }

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
            Keys.onEnterPressed: Keys.onSpacePressed(event)
            Keys.onReturnPressed: Keys.onSpacePressed(event)
        }
    }

    Component {
        id: dstdelegate

        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            Layout.topMargin: 1
            height: 61
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
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 60

                color: mouseOver ? "#f5f5f5" : "#ffffff"
                property bool mouseOver: false

                RowLayout {
                    anchors.fill: parent

                    Item {
                        width: 25
                    }

                    Image {
                        id: dstitem_image
                        source: isUsb ? "icons/ic_usb_40px.svg" : isScsi ? "icons/ic_storage_40px.svg" : "icons/ic_sd_storage_40px.svg"
                        verticalAlignment: Image.AlignVCenter
                        fillMode: Image.Pad
                        width: 64
                        height: 60
                    }

                    Item {
                        width: 25
                    }

                    ColumnLayout {
                        Text {
                            textFormat: Text.StyledText
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                            font.family: roboto.name
                            font.pointSize: 16
                            color: isReadOnly ? "grey" : "";
                            text: {
                                var sizeStr = (size/1000000000).toFixed(1)+ " " + qsTr("GB");
                                return description + " - " + sizeStr;
                            }

                        }
                        Text {
                            textFormat: Text.StyledText
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                            font.family: roboto.name
                            font.pointSize: 12
                            color: "grey"
                            text: {
                                var txt= qsTr("Mounted as %1").arg(mountpoints.join(", "));
                                if (isReadOnly) {
                                    txt += " " + qsTr("[WRITE PROTECTED]")
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
        onOpened: {
            forceActiveFocus()
        }
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
        modal: true
        onYes: {
            langbarRect.visible = false
            writebutton.visible = false
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
            hwbutton.enabled = false
            imageWriter.setVerifyEnabled(true)
            imageWriter.startWrite()
        }

        function askForConfirmation()
        {
            text = qsTr("All existing data on '%1' will be erased.<br>Are you sure you want to continue?").arg(dstbutton.text)
            openPopup()
        }

        onOpened: {
            forceActiveFocus()
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
        minimumWidth: 450
        minimumHeight: 400
        id: optionspopup
        onSaveSettingsSignal: {
            imageWriter.setSavedCustomizationSettings(settings)
            usesavedsettingspopup.hasSavedSettings = true
        }
    }

    UseSavedSettingsPopup {
        id: usesavedsettingspopup
        onYes: {
            optionspopup.initialize()
            optionspopup.applySettings()
            confirmwritepopup.askForConfirmation()
        }
        onNo: {
            imageWriter.setImageCustomization("", "", "", "", "")
            confirmwritepopup.askForConfirmation()
        }
        onNoClearSettings: {
            hasSavedSettings = false
            optionspopup.clearCustomizationFields()
            imageWriter.clearSavedCustomizationSettings()
            confirmwritepopup.askForConfirmation()
        }
        onEditSettings: {
            optionspopup.openPopup()
        }
        onCloseSettings: {
            optionspopup.close()
        }
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

    function onOsListPrepared() {
        fetchOSlist()
    }

    function resetWriteButton() {
        progressText.visible = false
        progressBar.visible = false
        osbutton.enabled = true
        dstbutton.enabled = true
        hwbutton.enabled = true
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
        else if (imageWriter.isEmbeddedMode()) {
            //msgpopup.text = qsTr("<b>%1</b> has been written to <b>%2</b>").arg(osbutton.text).arg(dstbutton.text)
            /* Just reboot to the installed OS */
            Qt.quit()
        }
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
        osswipeview.decrementCurrentIndex()
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

    function onNetworkInfo(msg) {
        networkInfo.text = msg
    }

    function shuffle(arr) {
        for (var i = 0; i < arr.length - 1; i++) {
            var j = i + Math.floor(Math.random() * (arr.length - i));

            var t = arr[j];
            arr[j] = arr[i];
            arr[i] = t;
        }
    }

    function checkForRandom(list) {
        for (var i in list) {
            var entry = list[i]

            if ("subitems" in entry) {
                checkForRandom(entry["subitems"])
                if ("random" in entry && entry["random"]) {
                    shuffle(entry["subitems"])
                }
            }
        }
    }

    function filterItems(list, tags, matchingType)
    {
        if (!tags || !tags.length)
            return

        var i = list.length
        while (i--) {
            var entry = list[i]

            if ("devices" in entry && entry["devices"].length) {
                var foundTag = false

                switch(matchingType) {
                case 0: /* exact matching */
                case 2: /* exact matching */
                    for (var j in tags)
                    {
                        if (entry["devices"].includes(tags[j]))
                        {
                            foundTag = true
                            break
                        }
                    }
                    /* If there's no match, remove this item from the list. */
                    if (!foundTag)
                    {
                        list.splice(i, 1)
                        continue
                    }
                    break
                case 1: /* Exlusive by prefix matching */
                case 3: /* Inclusive by prefix matching */
                    for (var deviceTypePrefix in tags) {
                        for (var deviceSpec in entry["devices"]) {
                            if (deviceSpec.startsWith(deviceTypePrefix)) {
                                foundTag = true
                                break
                            }
                        }
                        /* Terminate outer loop early if we've already
                             * decided it's a match
                             */
                        if (foundTag) {
                            break
                        }
                    }
                    /* If there's no match, remove this item from the list. */
                    if (!foundTag)
                    {
                        list.splice(i, 1)
                        continue
                    }
                    break
                }
            } else {
                /* No device list attached? If we're in an exclusive mode that's bad news indeed. */
                switch (matchingType) {
                case 0:
                case 1:
                    if (!("subitems" in entry)) {
                        /* If you're not carrying subitems, you're not going in. */
                        list.splice(i, 1)
                    }
                    break
                case 2:
                case 3:
                    /* Inclusive filtering. We're keeping this one. */
                    break;
                }
            }

            if ("subitems" in entry) {
                filterItems(entry["subitems"], tags, hwTagMatchingType)

                // If this sub-list has no items then hide it
                if (entry["subitems"].length == 0) {
                    list.splice(i, 1)
                }
            }
        }
    }

    function oslistFromJson(o) {
        var oslist_parsed = false
        var lang_country = Qt.locale().name
        if ("os_list_"+lang_country in o) {
            oslist_parsed = o["os_list_"+lang_country]
        }
        else if (lang_country.includes("_")) {
            var lang = lang_country.substr(0, lang_country.indexOf("_"))
            if ("os_list_"+lang in o) {
                oslist_parsed = o["os_list_"+lang]
            }
        }

        if (!oslist_parsed) {
            if (!"os_list" in o) {
                onError(qsTr("Error parsing os_list.json"))
                return false
            }

            oslist_parsed = o["os_list"]
        }

        checkForRandom(oslist_parsed)

        /* Flatten subitems to subitems_json */
        for (var i in oslist_parsed) {
            var entry = oslist_parsed[i];
            if ("subitems" in entry) {
                entry["subitems_json"] = JSON.stringify(entry["subitems"])
                delete entry["subitems"]
            }
        }

        return oslist_parsed
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

    function fetchOSlist() {
        var oslist_json = imageWriter.getFilteredOSlist();
        var o = JSON.parse(oslist_json)
        var oslist_parsed = oslistFromJson(o)
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
                deviceModel.clear()
                var devices = imager["devices"]
                for (var j in devices)
                {
                    devices[j]["tags"] = JSON.stringify(devices[j]["tags"])
                    deviceModel.append(devices[j])
                    if ("default" in devices[j] && devices[j]["default"])
                    {
                        hwlist.currentIndex = deviceModel.count-1
                    }
                }
            }

            if (imageWriter.getBoolSetting("check_version") && "latest_version" in imager && "url" in imager) {
                if (!imageWriter.isEmbeddedMode() && imageWriter.isVersionNewer(imager["latest_version"])) {
                    updatepopup.url = imager["url"]
                    updatepopup.openPopup()
                }
            }
            if ("default_os" in imager) {
                selectNamedOS(imager["default_os"], osmodel)
            }
            if (imageWriter.isEmbeddedMode()) {
                if ("embedded_default_os" in imager) {
                    selectNamedOS(imager["embedded_default_os"], osmodel)
                }
                if ("embedded_default_destination" in imager) {
                    imageWriter.startDriveListPolling()
                    setDefaultDest.drive = imager["embedded_default_destination"]
                    setDefaultDest.start()
                }
            }
        }
    }

    Timer {
        /* Verify if default drive is in our list after 100 ms */
        id: setDefaultDest
        property string drive : ""
        interval: 100
        onTriggered: {
            for (var i = 0; i < driveListModel.rowCount(); i++)
            {
                /* FIXME: there should be a better way to iterate drivelist than
                   fetch data by numeric role number */
                if (driveListModel.data(driveListModel.index(i,0), 0x101) === drive) {
                    selectDstItem({
                                      device: drive,
                                      description: driveListModel.data(driveListModel.index(i,0), 0x102),
                                      size: driveListModel.data(driveListModel.index(i,0), 0x103),
                                      readonly: false
                                  })
                    break
                }
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
        hwpopup.close()
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
