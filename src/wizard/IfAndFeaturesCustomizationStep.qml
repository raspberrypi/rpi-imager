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

    title: qsTr("Customization: Interfaces & Features")
    subtitle: qsTr("Enable hardware interfaces and connectivity options.")
    showSkipButton: true

    function updateCaps() {
        // imageWriter knows the currently selected hardware
        supportsSerialConsoleOnly = imageWriter.checkHWCapability("serial_on_console_only")
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
                                editable: true
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

                            ImOptionPill {
                                id: chkEnablePiConnect
                                Layout.fillWidth: true
                                text: qsTr("Enable Raspberry Pi Connect")
                                helpLabel: qsTr("Learn more about RPi-Connect")
                                helpUrl: "https://www.raspberrypi.com/documentation/services/connect.html"
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
            // skip page
            wizardContainer.jumpToStep(wizardContainer.stepWriting)
            return
        }

        updateCaps()

        if (supportsSerialConsoleOnly) {
            serialModel.append({ text: qsTr("Console") })
        }

        root.registerFocusGroup("if_section_interfaces", function() {
            return [chkEnableI2C, chkEnableSPI, comboSerial]
        }, 0)

        root.registerFocusGroup("if_section_features", function() {
            return [chkEnablePiConnect /* , chkEnableUsbGadget if we add it */]
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
        chkEnablePiConnect.checked = saved.enablePiConnect === true || saved.enablePiConnect === "true" || wizardContainer.featPiConnectEnabled

        // If we add USB ethernet gadget:
        //if (wizardContainer.supportsUsbGadget) {
        //    // chkEnableUsbGadget.checked = saved.enableUsbGadget === true || saved.enableUsbGadget === "true" || wizardContainer.ifUsbGadgetEnabled
        //}
    }

    onNextClicked: {
        let saved = imageWriter.getSavedCustomizationSettings()

        // Interfaces
        saved.enableI2C    = chkEnableI2C.checked
        saved.enableSPI    = chkEnableSPI.checked
        saved.enableSerial = !supportsSerialConsoleOnly && comboSerial.editText === "Console" ? "Default" : comboSerial.editText

        // Features
        saved.enablePiConnect = chkEnablePiConnect.checked
        // If we add USB ethernet gadget (and gate by model support):
        // if (wizardContainer.supportsUsbGadget === true) saved.enableUsbGadget = chkEnableUsbGadget.checked
        // else delete saved.enableUsbGadget

        imageWriter.setSavedCustomizationSettings(saved)

        // Mirror into wizardContainer
        wizardContainer.ifI2cEnabled     = chkEnableI2C.checked
        wizardContainer.ifSpiEnabled     = chkEnableSPI.checked
        wizardContainer.ifSerial         = comboSerial.editText
        wizardContainer.featPiConnectEnabled = chkEnablePiConnect.checked
    }

    onSkipClicked: {
        let saved = imageWriter.getSavedCustomizationSettings()
        delete saved.enableI2C
        delete saved.enableSPI
        delete saved.enableSerial
        delete saved.enablePiConnect
        imageWriter.setSavedCustomizationSettings(saved)

        // Clear all customization flags
        wizardContainer.ifI2cEnabled = false
        wizardContainer.ifSpiEnabled = false
        wizardContainer.ifSerial = "Disabled"
        wizardContainer.featPiConnectEnabled = false

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
