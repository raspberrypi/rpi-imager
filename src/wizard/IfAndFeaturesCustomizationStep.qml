/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtCore
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root

    required property ImageWriter imageWriter
    required property var wizardContainer

    property bool supportsSerialConsoleOnly: false
    property bool supportsUsbOtg: false
    property bool isConfirmed: false

    title: qsTr("Customization: Interfaces & Features")
    subtitle: qsTr("Enable hardware interfaces and connectivity options.")
    showSkipButton: true
    nextButtonEnabled: true
    backButtonEnabled: true

    function updateCaps() {
        // imageWriter knows the currently selected hardware
        supportsSerialConsoleOnly = imageWriter.checkHWCapability("serial_on_console_only")
        supportsUsbOtg = imageWriter.checkHWAndSWCapability("usb_otg")
    }

    content: [
        ScrollView {
            id: scroller
            anchors.fill: parent
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ColumnLayout {
                id: scrollContent
                width: scroller.availableWidth
                spacing: Style.stepContentSpacing

                // === Interfaces ===
                WizardSectionContainer {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.spacingSmall

                        // Section title
                        Label {
                            text: qsTr("Interfaces")
                            font.bold: true
                            font.pixelSize: Style.fontSizeTitle
                            Layout.alignment: Qt.AlignLeft
                        }

                        // Interface toggles
                        ImOptionPill {
                            id: chkEnableI2C
                            Layout.fillWidth: true
                            text: qsTr("Enable I²C")
                            checked: false
                        }

                        ImOptionPill {
                            id: chkEnableSPI
                            Layout.fillWidth: true
                            text: qsTr("Enable SPI")
                            checked: false
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.spacingMedium

                            WizardFormLabel { text: qsTr("Enable Serial:"); font.bold: true; Layout.fillWidth: true }
                            ImComboBox {
                                id: comboSerial
                                model: ListModel {
                                    id: serialModel
                                    ListElement { text: "Disabled" }
                                    ListElement { text: "Default" }
                                    ListElement { text: "Console & Hardware" }
                                    ListElement { text: "Hardware" }
                                }
                                Layout.fillWidth: false
                                editable: false
                                selectTextByMouse: false
                                activeFocusOnTab: true
                                font.pixelSize: Style.fontSizeInput
                                Layout.alignment: Qt.AlignRight
                            }
                        }
                    }
                }

                // === Features ===
                WizardSectionContainer {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Style.spacingSmall

                        // Section title
                        Label {
                            text: qsTr("Features")
                            font.bold: true
                            font.pixelSize: Style.fontSizeTitle
                            Layout.alignment: Qt.AlignLeft
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: supportsUsbOtg

                            ImOptionPill {
                                id: chkEnableUsbGadget
                                Layout.fillWidth: true
                                text: qsTr("Enable USB Gadget Mode")
                                helpLabel: imageWriter.isEmbeddedMode() ? "" : qsTr("Learn more about USB Gadget Mode")
                                helpUrl: imageWriter.isEmbeddedMode() ? "" : "https://www.raspberrypi.com/documentation/computers/usb-gadget.html"
                                checked: false
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    ]

    Component.onCompleted: {
        if (!imageWriter.checkSWCapability("rpios_cloudinit")) {
            root.isConfirmed = true
            wizardContainer.ifI2cEnabled     = false
            wizardContainer.ifSpiEnabled     = false
            wizardContainer.ifSerial         = false
            wizardContainer.featUsbGadgetEnabled = false
            // skip page
            wizardContainer.nextStep()
            return
        }
        root.isConfirmed = false

        updateCaps()

        if (supportsSerialConsoleOnly) {
            serialModel.append({ text: qsTr("Console") })
        }

        root.registerFocusGroup("if_section_interfaces", function() {
            return [chkEnableI2C, chkEnableSPI, comboSerial]
        }, 0)

        root.registerFocusGroup("if_section_features", function() {
            return [chkEnableUsbGadget]
        }, 1)

        // Prefill
        var saved = imageWriter.getSavedCustomizationSettings()
        // Load from persisted settings (if you store them there)…
        chkEnableI2C.checked     = saved.enableI2C === true || saved.enableI2C === "true" || wizardContainer.ifI2cEnabled
        chkEnableSPI.checked     = saved.enableSPI === true || saved.enableSPI === "true" || wizardContainer.ifSpiEnabled
        var enableSerial         = saved.enableSerial || wizardContainer.ifSerial
        var idx                  = comboSerial.find(enableSerial)
        comboSerial.currentIndex = (idx >= 0 ? idx : 0)

        // Features
        if (supportsUsbOtg) {
            chkEnableUsbGadget.checked = saved.enableUsbGadget === true || saved.enableUsbGadget === "true" || wizardContainer.featUsbGadgetEnabled
        } else {
            chkEnableUsbGadget.checked = false
        }
    }

    onNextClicked: {
        let saved = imageWriter.getSavedCustomizationSettings()

        // Interfaces
        saved.enableI2C    = chkEnableI2C.checked
        saved.enableSPI    = chkEnableSPI.checked
        saved.enableSerial = !supportsSerialConsoleOnly && comboSerial.editText === "Console" ? "Default" : comboSerial.editText

        // Features
        saved.enableUsbGadget = supportsUsbOtg ? chkEnableUsbGadget.checked : false

        imageWriter.setSavedCustomizationSettings(saved)

        // Mirror into wizardContainer
        wizardContainer.ifI2cEnabled     = chkEnableI2C.checked
        wizardContainer.ifSpiEnabled     = chkEnableSPI.checked
        wizardContainer.ifSerial         = comboSerial.editText
        wizardContainer.featUsbGadgetEnabled = saved.enableUsbGadget

        if (saved.enableUsbGadget && !imageWriter.getBoolSetting("disable_warnings")) {
            confirmDialog.open()
        } else {
            root.isConfirmed = true
        }
    }

    // Confirmation dialog
    Dialog {
        id: confirmDialog
        modal: true
        parent: wizardContainer && wizardContainer.overlayRootRef ? wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton
        visible: false

        property bool allowAccept: false
        title: qsTr("Enable USB Gadget Mode")

        background: Rectangle {
            color: Style.mainBackgroundColor
            radius: Style.sectionBorderRadius
            border.color: Style.popupBorderColor
            border.width: Style.sectionBorderWidth
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.cardPadding
            spacing: Style.spacingMedium

            Text {
                text: qsTr("USB Gadget Mode can change how your device behaves and may impact connectivity and host interaction.")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelErrorColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Text {
                textFormat: Text.RichText
                text: qsTr("Please review the <a href='%1'>documentation</a> before proceeding.").arg(chkEnableUsbGadget.helpUrl)
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                color: Style.formLabelColor
                wrapMode: Text.WordWrap
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("Only continue if you are sure you know what you are doing.")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                color: Style.formLabelErrorColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                Item { Layout.fillWidth: true }

                ImButton {
                    text: qsTr("Cancel")
                    onClicked: confirmDialog.close()
                }

                ImButtonRed {
                    id: acceptBtn
                    text: qsTr("I understand, continue")
                    onClicked: {
                        confirmDialog.close()
                        root.isConfirmed = true
                        // Advance to next step
                        wizardContainer.nextStep()
                    }
                }
            }
        }

        // Delay accept for 2 seconds
        Timer {
            id: confirmDelay
            interval: 2000
            running: false
            repeat: false
            onTriggered: confirmDialog.allowAccept = true
        }

        // Ensure disabled before showing to avoid flicker
        onAboutToShow: {
            allowAccept = false
            confirmDelay.start()
        }
        onClosed: {
            confirmDelay.stop()
            allowAccept = false
        }
    }

    onSkipClicked: {
        let saved = imageWriter.getSavedCustomizationSettings()
        delete saved.enableI2C
        delete saved.enableSPI
        delete saved.enableSerial
        delete saved.enableUsbGadget
        imageWriter.setSavedCustomizationSettings(saved)

        // Clear all customization flags
        wizardContainer.ifI2cEnabled = false
        wizardContainer.ifSpiEnabled = false
        wizardContainer.ifSerial = "Disabled"
        wizardContainer.featUsbGadgetEnabled = false

        // Jump to writing step
        wizardContainer.jumpToStep(wizardContainer.stepWriting)
    }

    Connections {
        // Recompute caps if the selected device changes elsewhere
        target: wizardContainer
        function onSelectedDeviceNameChanged() {
            updateCaps()
            // If Console is no longer supported and was selected, fall back
            if (!supportsSerialConsoleOnly && comboSerial.editText === qsTr("Console"))
                comboSerial.currentIndex = 0;
        }
    }
}
