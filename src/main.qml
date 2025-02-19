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

import RpiImager

ApplicationWindow {
    id: window
    visible: true

    required property ImageWriter imageWriter
    required property DriveListModel driveListModel

    width: imageWriter.isEmbeddedMode() ? -1 : 680
    height: imageWriter.isEmbeddedMode() ? -1 : 450
    minimumWidth: imageWriter.isEmbeddedMode() ? -1 : 680
    minimumHeight: imageWriter.isEmbeddedMode() ? -1 : 420

    title: qsTr("Raspberry Pi Imager v%1").arg(imageWriter.constantVersion())

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
            color: Style.mainBackgroundColor
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
                        color: Style.subtitleColor
                        text: qsTr("Raspberry Pi Device")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        Layout.preferredWidth: 100
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ImButton {
                        id: hwbutton
                        text: window.imageWriter.getHWList().currentName
                        spacing: 0
                        padding: 0
                        bottomPadding: 0
                        topPadding: 0
                        Layout.minimumHeight: 40
                        Layout.fillWidth: true
                        onClicked: {
                            hwpopup.open()
                            hwpopup.hwlist.forceActiveFocus()
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
                        color: Style.subtitleColor
                        text: qsTr("Operating System")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ImButton {
                        id: osbutton
                        text: window.imageWriter.srcFileName() === "" ? qsTr("CHOOSE OS") : window.imageWriter.srcFileName()
                        spacing: 0
                        padding: 0
                        bottomPadding: 0
                        topPadding: 0
                        // text: window.imageWriter.getOSList().currentName TODO
                        Layout.minimumHeight: 40
                        Layout.fillWidth: true
                        onClicked: {
                            ospopup.open()
                            ospopup.osswipeview.currentItem.forceActiveFocus()
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
                        color: Style.subtitleColor
                        text: qsTr("Storage")
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
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
                            window.imageWriter.startDriveListPolling()
                            dstpopup.open()
                            dstpopup.dstlist.forceActiveFocus()
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
                        color: Style.progressBarTextColor
                        font.family: Style.fontFamilyBold
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
                        Material.background: Style.progressBarBackgroundColor
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
                            window.imageWriter.cancelWrite()
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
                            window.imageWriter.setVerifyEnabled(false)
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
                            if (!window.imageWriter.readyToWrite()) {
                                return
                            }

                            if (!optionspopup.visible && window.imageWriter.imageSupportsCustomization()) {
                                usesavedsettingspopup.openPopup()
                            } else {
                                confirmwritepopup.askForConfirmation()
                            }
                        }
                    }
                }

                Text {
                    Layout.columnSpan: 3
                    color: Style.embeddedModeInfoTextColor
                    font.pixelSize: 18
                    font.family: Style.fontFamily
                    visible: window.imageWriter.isEmbeddedMode() && window.imageWriter.customRepo()
                    text: qsTr("Using custom repository: %1").arg(window.imageWriter.constantOsListUrl())
                }

                Text {
                    id: networkInfo
                    Layout.columnSpan: 3
                    color: Style.embeddedModeInfoTextColor
                    font.pixelSize: 18
                    font.family: Style.fontFamily
                    visible: window.imageWriter.isEmbeddedMode()
                    text: qsTr("Network not ready yet")
                }

                Text {
                    Layout.columnSpan: 3
                    color: Style.embeddedModeInfoTextColor
                    font.pixelSize: 18
                    font.family: Style.fontFamily
                    visible: !window.imageWriter.hasMouse()
                    text: qsTr("Keyboard navigation: <tab> navigate to next button <space> press button/select item <arrow up/down> go up/down in lists")
                }

                Rectangle {
                    id: langbarRect
                    Layout.columnSpan: 3
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                    Layout.bottomMargin: 5
                    visible: window.imageWriter.isEmbeddedMode()
                    implicitWidth: langbar.width
                    implicitHeight: langbar.height
                    color: Style.lanbarBackgroundColor
                    radius: 5

                    RowLayout {
                        id: langbar
                        spacing: 10

                        Text {
                            font.pixelSize: 12
                            font.family: Style.fontFamily
                            text: qsTr("Language: ")
                            Layout.leftMargin: 30
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                        }
                        ComboBox {
                            font.pixelSize: 12
                            font.family: Style.fontFamily
                            model: window.imageWriter.getTranslations()
                            Layout.preferredWidth: 200
                            currentIndex: -1
                            Component.onCompleted: {
                                currentIndex = find(window.imageWriter.getCurrentLanguage())
                            }
                            onActivated: {
                                window.imageWriter.changeLanguage(editText)
                            }
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                        }
                        Text {
                            font.pixelSize: 12
                            font.family: Style.fontFamily
                            text: qsTr("Keyboard: ")
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                        }
                        ComboBox {
                            enabled: window.imageWriter.isEmbeddedMode()
                            font.pixelSize: 12
                            font.family: Style.fontFamily
                            model: window.imageWriter.getKeymapLayoutList()
                            currentIndex: -1
                            Component.onCompleted: {
                                currentIndex = find(window.imageWriter.getCurrentKeyboard())
                            }
                            onActivated: {
                                window.imageWriter.changeKeyboard(editText)
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
                onEntered: (drag) => {
                    if (Drag.active && drag.hasUrls) {
                        drag.acceptProposedAction()
                    }
                }
                onDropped: (drop) => {
                    if (drop.urls && drop.urls.length > 0) {
                        window.onFileSelected(drop.urls[0].toString())
                    }
                }
            }
        }
    }

    HwPopup {
        id: hwpopup
        windowWidth: window.width
        imageWriter: window.imageWriter

        onDeviceSelected: {
            // When the HW device is changed, reset the OS selection otherwise
            // you get a weird effect with the selection moving around in the list
            // when the user next opens the OS list, and the user could still have
            // an OS selected which isn't compatible with this HW device
            ospopup.oslist.currentIndex = -1
            ospopup.osswipeview.currentIndex = 0
            osbutton.text = qsTr("CHOOSE OS")
            writebutton.enabled = false
        }
    }

    OSPopup {
        id: ospopup
        windowWidth: window.width
        imageWriter: window.imageWriter

        onUpdatePopupRequested: (url) => {
            updatepopup.url = url
            updatepopup.openPopup()
        }
    }

    DstPopup {
        id: dstpopup
        imageWriter: window.imageWriter
        driveListModel: window.driveListModel
        windowWidth: window.width
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
            window.imageWriter.setVerifyEnabled(true)
            window.imageWriter.startWrite()
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
        id: optionspopup
        minimumWidth: 450
        minimumHeight: 400

        imageWriter: window.imageWriter

        onSaveSettingsSignal: (settings) => {
            window.imageWriter.setSavedCustomizationSettings(settings)
            usesavedsettingspopup.hasSavedSettings = true
        }
    }

    UseSavedSettingsPopup {
        id: usesavedsettingspopup
        imageWriter: window.imageWriter

        onYes: {
            optionspopup.initialize()
            optionspopup.applySettings()
            confirmwritepopup.askForConfirmation()
        }
        onNo: {
            window.imageWriter.setImageCustomization("", "", "", "", "")
            confirmwritepopup.askForConfirmation()
        }
        onNoClearSettings: {
            hasSavedSettings = false
            optionspopup.clearCustomizationFields()
            window.imageWriter.clearSavedCustomizationSettings()
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
            progressBar.Material.accent = Style.progressBarVerifyForegroundColor
            progressBar.value = newPos
        }
    }

    function onPreparationStatusUpdate(msg) {
        progressText.text = qsTr("Preparing to write... (%1)").arg(msg)
    }

    function onOsListPrepared() {
        ospopup.fetchOSlist()
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
        ospopup.osswipeview.decrementCurrentIndex()
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

    Timer {
        /* Verify if default drive is in our list after 100 ms */
        id: setDefaultDest
        property string drive : ""
        interval: 100
        onTriggered: {
            for (var i = 0; i < window.driveListModel.rowCount(); i++)
            {
                /* FIXME: there should be a better way to iterate drivelist than
                   fetch data by numeric role number */
                if (window.driveListModel.data(window.driveListModel.index(i,0), 0x101) === drive) {
                    dstpopup.selectDstItem({
                                      device: drive,
                                      description: window.driveListModel.data(window.driveListModel.index(i,0), 0x102),
                                      size: window.driveListModel.data(window.driveListModel.index(i,0), 0x103),
                                      readonly: false
                                  })
                    break
                }
            }
        }
    }

    // Called from C++
    function fetchOSlist() {
        ospopup.fetchOSlist()
    }
}
