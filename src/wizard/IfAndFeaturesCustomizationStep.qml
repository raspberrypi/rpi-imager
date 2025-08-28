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
                            Layout.alignment: Qt.AlignLeft
                        }

                        // Interface toggles
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.spacingMedium

                            ImCheckBox {
                                id: chkEnableI2C
                                text: qsTr("Enable I²C")
                                checked: false
                                onCheckedChanged: root.requestRecomputeTabOrder()
                            }

                            ImCheckBox {
                                id: chkEnableSPI
                                text: qsTr("Enable SPI")
                                checked: false
                                onCheckedChanged: root.requestRecomputeTabOrder()
                            }

                            ImCheckBox {
                                id: chkEnableSerial
                                text: qsTr("Enable Serial")
                                checked: false
                                onCheckedChanged: root.requestRecomputeTabOrder()
                            }

                            Item { Layout.fillWidth: true }
                        }

                        // Serial mode selection (enabled only when serial is enabled)
                        ColumnLayout {
                            spacing: Style.spacingSmall
                            enabled: chkEnableSerial.checked

                            ButtonGroup { id: serialModeGroup; exclusive: true }

                            ImRadioButton {
                                id: rbSerialDefault
                                text: qsTr("Default")
                                checked: true
                                ButtonGroup.group: serialModeGroup
                                onCheckedChanged: root.requestRecomputeTabOrder()
                            }

                            ImRadioButton {
                                id: rbSerialConsoleAndHw
                                text: qsTr("Console & hardware")
                                checked: true
                                ButtonGroup.group: serialModeGroup
                                onCheckedChanged: root.requestRecomputeTabOrder()
                            }

                            ImRadioButton {
                                id: rbSerialConsoleOnly
                                text: qsTr("Console")
                                ButtonGroup.group: serialModeGroup
                                visible: supportsSerialConsoleOnly
                                onCheckedChanged: root.requestRecomputeTabOrder()
                                onVisibleChanged: if (!visible && checked) rbSerialConsoleAndHw.checked = true
                            }

                            ImRadioButton {
                                id: rbSerialHardwareOnly
                                text: qsTr("Hardware")
                                ButtonGroup.group: serialModeGroup
                                onCheckedChanged: root.requestRecomputeTabOrder()
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
                            Layout.alignment: Qt.AlignLeft
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.spacingMedium

                            ImCheckBox {
                                id: chkEnablePiConnect
                                text: qsTr("Enable Raspberry Pi Connect")
                                checked: false
                                onCheckedChanged: root.requestRecomputeTabOrder()
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    ]

    Component.onCompleted: {
        updateCaps()

        root.registerFocusGroup("if_section_interfaces", function() {
            const list = [chkEnableI2C, chkEnableSPI, chkEnableSerial]
            if (chkEnableSerial.checked) {
                // radios become tabbable only when serial is enabled
                list.push(rbSerialDefault)
                list.push(rbSerialConsoleAndHw)
                if (wizardContainer.selectedDeviceName === "Raspberry Pi 5")
                    list.push(rbSerialConsoleOnly)
                list.push(rbSerialHardwareOnly)
            }
            return list
        }, 0)

        root.registerFocusGroup("if_section_features", function() {
            return [chkEnablePiConnect /* , chkEnableUsbGadget if we add it */]
        }, 1)

        // Prefill
        var saved = imageWriter.getSavedCustomizationSettings()
        // Load from persisted settings (if you store them there)…
        chkEnableI2C.checked     = saved.enableI2C === true || saved.enableI2C === "true" || wizardContainer.ifI2cEnabled
        chkEnableSPI.checked     = saved.enableSPI === true || saved.enableSPI === "true" || wizardContainer.ifSpiEnabled
        chkEnableSerial.checked  = saved.enableSerial === true || saved.enableSerial === "true" || wizardContainer.ifSerialEnabled

        // Serial mode
        var mode = saved.serialMode || wizardContainer.ifSerialMode || "default"
        if (mode === "console" && !supportsSerialConsoleOnly) mode = "default"
        rbSerialDefault.cheched      = (mode === "default")
        rbSerialConsoleAndHw.checked = (mode === "console_hw")
        rbSerialHardwareOnly.checked = (mode === "hw")
        rbSerialConsoleOnly.checked  = (mode === "console" && supportsSerialConsoleOnly)

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
        saved.enableSerial = chkEnableSerial.checked

        let mode = ""
        if (chkEnableSerial.checked) {
            mode = rbSerialDefault.checked ? "default"
                : rbSerialConsoleAndHw.checked ? "console_hw"
                : rbSerialConsoleOnly.checked ? "console"
                : "hw"
            if (mode === "console" && !supportsSerialConsoleOnly)
                mode = "default"
            saved.serialMode = mode
        } else {
            delete saved.serialMode
        }

        // Features
        saved.enablePiConnect = chkEnablePiConnect.checked
        // If we add USB ethernet gadget (and gate by model support):
        // if (wizardContainer.supportsUsbGadget === true) saved.enableUsbGadget = chkEnableUsbGadget.checked
        // else delete saved.enableUsbGadget

        imageWriter.setSavedCustomizationSettings(saved)

        // Mirror into wizardContainer
        wizardContainer.ifI2cEnabled     = chkEnableI2C.checked
        wizardContainer.ifSpiEnabled     = chkEnableSPI.checked
        wizardContainer.ifSerialEnabled  = chkEnableSerial.checked
        wizardContainer.ifSerialMode     = chkEnableSerial.checked ? mode : ""
        wizardContainer.featPiConnectEnabled = chkEnablePiConnect.checked
    }

    onSkipClicked: {
        let saved = imageWriter.getSavedCustomizationSettings()
        delete saved.enableI2C
        delete saved.enableSPI
        delete saved.enableSerial
        delete saved.serialMode
        delete saved.enablePiConnect
        imageWriter.setSavedCustomizationSettings(saved)

        // Clear all customization flags
        wizardContainer.ifI2cEnabled = false
        wizardContainer.ifSpiEnabled = false
        wizardContainer.ifSerialEnabled = false
        wizardContainer.ifSerialMode = ""
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
            if (!supportsSerialConsoleOnly && rbSerialConsoleOnly.checked)
                rbSerialDefault.checked = true
        }
    }
}
