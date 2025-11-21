/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import QtCore
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root

    required property ImageWriter imageWriter
    required property var wizardContainer

    // Capability flags for each UI option
    property bool supportsI2c: false
    property bool supportsSpi: false
    property bool supports1Wire: false
    property bool supportsSerial: false
    property bool supportsSerialConsoleOnly: false
    property bool supportsUsbOtg: false
    property bool isConfirmed: false

    title: qsTr("Customisation: Interfaces & Features")
    subtitle: qsTr("Enable hardware interfaces and connectivity options.")
    showSkipButton: true
    nextButtonEnabled: true
    backButtonEnabled: true
    nextButtonAccessibleDescription: qsTr("Save interface and feature settings and continue to writing step")
    backButtonAccessibleDescription: qsTr("Return to previous step")
    skipButtonAccessibleDescription: qsTr("Skip all customisation and proceed directly to writing the image")

    function updateCaps() {
        // Check individual capabilities for each interface/feature
        supportsI2c = imageWriter.checkHWAndSWCapability("i2c")
        supportsSpi = imageWriter.checkHWAndSWCapability("spi")
        supports1Wire = imageWriter.checkHWAndSWCapability("onewire")
        supportsSerial = imageWriter.checkHWAndSWCapability("serial")
        supportsSerialConsoleOnly = imageWriter.checkHWCapability("serial_on_console_only")
        supportsUsbOtg = imageWriter.checkHWAndSWCapability("usb_otg")
    }

    content: [
        ScrollView {
            id: ifAndFeatScroll
            anchors.fill: parent
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            rightPadding: 20
            ColumnLayout {
                id: scrollContent
                width: ifAndFeatScroll.availableWidth
                spacing: Style.stepContentSpacing

                // === Interfaces ===
                WizardSectionContainer {
                    visible: supportsI2c || supportsSpi || supports1Wire || supportsSerial

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
                            text: qsTr("Enable I2C")
                            accessibleDescription: qsTr("Enable the I2C (Inter-Integrated Circuit) interface for connecting sensors and other low-speed peripherals")
                            checked: false
                            visible: supportsI2c
                        }

                        ImOptionPill {
                            id: chkEnableSPI
                            Layout.fillWidth: true
                            text: qsTr("Enable SPI")
                            accessibleDescription: qsTr("Enable the SPI (Serial Peripheral Interface) for high-speed communication with displays and sensors")
                            checked: false
                            visible: supportsSpi
                        }

                        ImOptionPill {
                            id: chkEnable1Wire
                            Layout.fillWidth: true
                            text: qsTr("Enable 1-Wire")
                            accessibleDescription: qsTr("Enable the 1-Wire interface for connecting temperature sensors and other Dallas/Maxim devices")
                            checked: false
                            visible: supports1Wire
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.spacingMedium
                            visible: supportsSerial

                            WizardFormLabel {
                                id: labelSerial
                                text: qsTr("Enable Serial:")
                                font.bold: true
                                Layout.fillWidth: true
                                Accessible.ignored: false
                                Accessible.focusable: true
                                Accessible.description: qsTr("Configure the serial interface: Disabled, Default (system decides), Console & Hardware (both console and UART), Hardware (UART only), or Console (console only on supported devices).")
                                focusPolicy: Qt.TabFocus
                                activeFocusOnTab: true
                            }
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
                    visible: supportsUsbOtg

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
                                id: chkEnableUsbGadget
                                Layout.fillWidth: true
                                text: qsTr("Enable USB Gadget Mode")
                                accessibleDescription: qsTr("Enable USB device mode to use your Raspberry Pi as a USB peripheral for networking and storage")
                                helpLabel: imageWriter.isEmbeddedMode() ? "" : qsTr("Learn more about USB Gadget Mode")
                                helpUrl: imageWriter.isEmbeddedMode() ? "" : "https://github.com/raspberrypi/rpi-usb-gadget?tab=readme-ov-file"
                                checked: false
                            }

                            Item { Layout.fillWidth: true; height: 40 }
                        }
                    }
                }
            }
        }
    ]

    Component.onCompleted: {
        if (!wizardContainer.ccRpiAvailable) {
            root.isConfirmed = true
            wizardContainer.ifAndFeaturesAvailable = false
            wizardContainer.ifI2cEnabled     = false
            wizardContainer.ifSpiEnabled     = false
            wizardContainer.if1WireEnabled   = false
            wizardContainer.ifSerial         = false
            wizardContainer.featUsbGadgetEnabled = false
            // skip page
            wizardContainer.nextStep()
            return
        }
        root.isConfirmed = false

        // Defer capability check to ensure imageWriter capabilities are fully available
        // and QML bindings are established
        Qt.callLater(function() {
            updateCaps()
            
            // Update availability flag for sidebar navigation
            var hasAnyCapabilities = supportsI2c || supportsSpi || supports1Wire || supportsSerial || supportsUsbOtg
            wizardContainer.ifAndFeaturesAvailable = hasAnyCapabilities
            
            // If no capabilities are available, skip this step entirely
            if (!hasAnyCapabilities) {
                root.isConfirmed = true
                wizardContainer.ifI2cEnabled     = false
                wizardContainer.ifSpiEnabled     = false
                wizardContainer.if1WireEnabled   = false
                wizardContainer.ifSerial         = "Disabled"
                wizardContainer.featUsbGadgetEnabled = false
                wizardContainer.nextStep()
                return
            }
            
            if (supportsSerialConsoleOnly) {
                serialModel.append({ text: qsTr("Console") })
            }
            
            // Include label before combo box so users hear the explanation first
            // Build focus group dynamically based on which interfaces are supported
            root.registerFocusGroup("if_section_interfaces", function() {
                var items = []
                if (supportsI2c) items.push(chkEnableI2C.focusItem)
                if (supportsSpi) items.push(chkEnableSPI.focusItem)
                if (supports1Wire) items.push(chkEnable1Wire.focusItem)
                if (supportsSerial) {
                    items.push(labelSerial)
                    items.push(comboSerial)
                }
                return items
            }, 0)

            root.registerFocusGroup("if_section_features", function() {
                if (!supportsUsbOtg) return []
                var items = [chkEnableUsbGadget.focusItem]
                // Include help link if visible
                if (chkEnableUsbGadget.helpLinkItem && chkEnableUsbGadget.helpLinkItem.visible)
                    items.push(chkEnableUsbGadget.helpLinkItem)
                return items
            }, 1)

            // Prefill from conserved customization settings
            var settings = wizardContainer.customizationSettings
            
            // Only restore settings for supported interfaces
            if (supportsI2c) {
                chkEnableI2C.checked = settings.enableI2C === true || settings.enableI2C === "true" || wizardContainer.ifI2cEnabled
            } else {
                chkEnableI2C.checked = false
            }
            
            if (supportsSpi) {
                chkEnableSPI.checked = settings.enableSPI === true || settings.enableSPI === "true" || wizardContainer.ifSpiEnabled
            } else {
                chkEnableSPI.checked = false
            }
            
            if (supports1Wire) {
                chkEnable1Wire.checked = settings.enable1Wire === true || settings.enable1Wire === "true" || wizardContainer.if1WireEnabled
            } else {
                chkEnable1Wire.checked = false
            }
            
            if (supportsSerial) {
                var enableSerial = settings.enableSerial || wizardContainer.ifSerial
                var idx = comboSerial.find(enableSerial)
                comboSerial.currentIndex = (idx >= 0 ? idx : 0)
            } else {
                comboSerial.currentIndex = 0
            }

            // Features
            if (supportsUsbOtg) {
                chkEnableUsbGadget.checked = settings.enableUsbGadget === true || settings.enableUsbGadget === "true" || wizardContainer.featUsbGadgetEnabled
            } else {
                chkEnableUsbGadget.checked = false
            }
            
            // Rebuild focus order after capabilities and initial values are set
            root.rebuildFocusOrder()
        })
    }

    onNextClicked: {
        // Only save values for supported interfaces/features
        var i2cVal = supportsI2c ? chkEnableI2C.checked : false
        var spiVal = supportsSpi ? chkEnableSPI.checked : false
        var oneWireVal = supports1Wire ? chkEnable1Wire.checked : false
        var serialVal = supportsSerial ? (!supportsSerialConsoleOnly && comboSerial.editText === "Console" ? "Default" : comboSerial.editText) : "Disabled"
        var usbGadgetVal = supportsUsbOtg ? chkEnableUsbGadget.checked : false
        
        // Update conserved customization settings (runtime state)
        wizardContainer.customizationSettings.enableI2C    = i2cVal
        wizardContainer.customizationSettings.enableSPI    = spiVal
        wizardContainer.customizationSettings.enable1Wire  = oneWireVal
        wizardContainer.customizationSettings.enableSerial = serialVal
        wizardContainer.customizationSettings.enableUsbGadget = usbGadgetVal

        // Persist for future sessions
        imageWriter.setPersistedCustomisationSetting("enableI2C", i2cVal)
        imageWriter.setPersistedCustomisationSetting("enableSPI", spiVal)
        imageWriter.setPersistedCustomisationSetting("enable1Wire", oneWireVal)
        imageWriter.setPersistedCustomisationSetting("enableSerial", serialVal)
        imageWriter.setPersistedCustomisationSetting("enableUsbGadget", usbGadgetVal)

        // Mirror into wizardContainer
        wizardContainer.ifI2cEnabled     = i2cVal
        wizardContainer.ifSpiEnabled     = spiVal
        wizardContainer.if1WireEnabled   = oneWireVal
        wizardContainer.ifSerial         = supportsSerial ? comboSerial.editText : "Disabled"
        wizardContainer.featUsbGadgetEnabled = usbGadgetVal

        if (usbGadgetVal && !wizardContainer.disableWarnings) {
            confirmDialog.open()
        } else {
            root.isConfirmed = true
        }
    }

    // Confirmation dialog
    BaseDialog {
        id: confirmDialog
        imageWriter: root.imageWriter
        parent: wizardContainer && wizardContainer.overlayRootRef ? wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        visible: false
        title: qsTr("USB Gadget Mode Warning")

        property bool allowAccept: false

        // Custom escape handling
        function escapePressed() {
            confirmDialog.close()
        }

        // Register focus groups when component is ready
        Component.onCompleted: {
            registerFocusGroup("buttons", function(){ 
                return [cancelBtn, acceptBtn] 
            }, 0)
        }

        // Ensure disabled before showing to avoid flicker
        onOpened: {
            allowAccept = false
            confirmDelay.start()
        }
        onClosed: {
            confirmDelay.stop()
            allowAccept = false
        }

        // Dialog content
        Text {
            text: qsTr("USB Gadget Mode can change how your device behaves and may impact connectivity and host interaction.")
            font.pixelSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Text {
            textFormat: Text.RichText
            text: qsTr("Please review the <a href='%1'>documentation</a> before proceeding.").arg(chkEnableUsbGadget.helpUrl)
            font.pixelSize: Style.fontSizeFormLabel
            font.family: Style.fontFamilyBold
            color: Style.formLabelColor
            wrapMode: Text.WordWrap
            onLinkActivated: function(link) {
                if (imageWriter) {
                    imageWriter.openUrl(link)
                } else {
                    Qt.openUrlExternally(link)
                }
            }
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Please review the documentation before proceeding.")
        }

        Text {
            text: qsTr("Only continue if you are sure you know what you are doing.")
            font.pixelSize: Style.fontSizeFormLabel
            font.family: Style.fontFamilyBold
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }

            ImButton {
                id: cancelBtn
                text: CommonStrings.cancel
                accessibleDescription: qsTr("Cancel and return to the interfaces and features settings without enabling USB Gadget Mode")
                activeFocusOnTab: true
                onClicked: confirmDialog.close()
            }

            ImButtonRed {
                id: acceptBtn
                text: qsTr("I understand, continue")
                accessibleDescription: confirmDialog.allowAccept ? qsTr("Confirm that you understand the risks and continue with USB Gadget Mode enabled") : qsTr("This button will be enabled after 2 seconds")
                enabled: confirmDialog.allowAccept
                activeFocusOnTab: true
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
        onTriggered: {
            confirmDialog.allowAccept = true
            // Rebuild focus order now that accept button is enabled
            confirmDialog.rebuildFocusOrder()
        }
    }

    onSkipClicked: {
        // Clear all customization flags
        wizardContainer.hostnameConfigured = false
        wizardContainer.localeConfigured = false
        wizardContainer.userConfigured = false
        wizardContainer.wifiConfigured = false
        wizardContainer.sshEnabled = false
        wizardContainer.piConnectEnabled = false
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
            // Defer capability check to ensure capabilities are fully propagated
            Qt.callLater(function() {
                updateCaps()
                
                // Update availability flag for sidebar navigation
                var hasAnyCapabilities = supportsI2c || supportsSpi || supports1Wire || supportsSerial || supportsUsbOtg
                wizardContainer.ifAndFeaturesAvailable = hasAnyCapabilities
                
                // Rebuild focus order based on new capabilities
                root.rebuildFocusOrder()
                // If Console is no longer supported and was selected, fall back
                if (!supportsSerialConsoleOnly && comboSerial.editText === qsTr("Console"))
                    comboSerial.currentIndex = 0;
            })
        }
    }
}
