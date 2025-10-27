/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("Write complete!")
    showBackButton: false
    showNextButton: false
    readonly property bool autoEjectEnabled: imageWriter.getBoolSetting("eject")
    readonly property bool anyCustomizationsApplied: (
        wizardContainer.customizationSupported && (
            wizardContainer.hostnameConfigured ||
            wizardContainer.localeConfigured ||
            wizardContainer.userConfigured ||
            wizardContainer.wifiConfigured ||
            wizardContainer.sshEnabled ||
            wizardContainer.piConnectEnabled ||
            wizardContainer.ifI2cEnabled ||
            wizardContainer.ifSpiEnabled ||
            wizardContainer.if1WireEnabled ||
            wizardContainer.ifSerial !== ""  && wizardContainer.ifSerial !== "Disabled" ||
            wizardContainer.featUsbGadgetEnabled
        )
    )
    
    // Content
    content: [
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.cardPadding
        spacing: Style.spacingLarge
        
        // What was configured (de-chromed)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            
            Text {
                id: choicesHeading
                text: qsTr("Your choices:")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
                Accessible.focusable: true
                focusPolicy: Qt.TabFocus
                activeFocusOnTab: true
            }
            
            GridLayout {
                id: choicesGrid
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.spacingSmall
                
                Text {
                    id: deviceLabel
                    text: CommonStrings.device
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text + ": " + (wizardContainer.selectedDeviceName || CommonStrings.noDeviceSelected)
                    Accessible.focusable: true
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }
                Text {
                    text: wizardContainer.selectedDeviceName || CommonStrings.noDeviceSelected
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
                
                Text {
                    id: osLabel
                    text: qsTr("Operating system:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text + " " + (wizardContainer.selectedOsName || CommonStrings.noImageSelected)
                    Accessible.focusable: true
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }
                Text {
                    text: wizardContainer.selectedOsName || CommonStrings.noImageSelected
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
                
                Text {
                    id: storageLabel
                    text: qsTr("Storage:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text + " " + (wizardContainer.selectedStorageName || CommonStrings.noStorageSelected)
                    Accessible.focusable: true
                    focusPolicy: Qt.TabFocus
                    activeFocusOnTab: true
                }
                Text {
                    text: wizardContainer.selectedStorageName || CommonStrings.noStorageSelected
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
            }
            
            // Customization summary
            Text {
                id: customizationsHeading
                text: qsTr("Customisations applied:")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                Layout.topMargin: Style.spacingSmall
                visible: root.anyCustomizationsApplied
                Accessible.role: Accessible.Heading
                Accessible.name: text
                Accessible.focusable: true
                focusPolicy: Qt.TabFocus
                activeFocusOnTab: true
            }
            
            ScrollView {
                id: customizationScrollView
                Layout.fillWidth: true
                Layout.maximumHeight: Math.round(root.height * 0.4)
                clip: true
                visible: root.anyCustomizationsApplied
                activeFocusOnTab: true
                Accessible.role: Accessible.List
                Accessible.name: {
                    // Build a list of visible customizations to announce
                    var items = []
                    if (wizardContainer.hostnameConfigured) items.push(CommonStrings.hostnameConfigured)
                    if (wizardContainer.localeConfigured) items.push(CommonStrings.localeConfigured)
                    if (wizardContainer.userConfigured) items.push(CommonStrings.userAccountConfigured)
                    if (wizardContainer.wifiConfigured) items.push(CommonStrings.wifiConfigured)
                    if (wizardContainer.sshEnabled) items.push(CommonStrings.sshEnabled)
                    if (wizardContainer.piConnectEnabled) items.push(CommonStrings.piConnectEnabled)
                    if (wizardContainer.featUsbGadgetEnabled) items.push(CommonStrings.usbGadgetEnabled)
                    if (wizardContainer.ifI2cEnabled) items.push(CommonStrings.i2cEnabled)
                    if (wizardContainer.ifSpiEnabled) items.push(CommonStrings.spiEnabled)
                    if (wizardContainer.if1WireEnabled) items.push(CommonStrings.onewireEnabled)
                    if (wizardContainer.ifSerial !== "" && wizardContainer.ifSerial !== "Disabled") items.push(CommonStrings.serialConfigured)
                    
                    return items.length + " " + (items.length === 1 ? qsTr("customization") : qsTr("customizations")) + ": " + items.join(", ")
                }
                Flickable {
                    contentWidth: parent.width
                    contentHeight: customizationColumn.height
                    
                    Keys.onUpPressed: {
                        if (contentY > 0) {
                            contentY = Math.max(0, contentY - 20)
                        }
                    }
                    Keys.onDownPressed: {
                        var maxY = Math.max(0, contentHeight - height)
                        if (contentY < maxY) {
                            contentY = Math.min(maxY, contentY + 20)
                        }
                    }
                    
                    Column {
                        id: customizationColumn
                        width: parent.width
                        Text { text: "✓ " + CommonStrings.hostnameConfigured; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.hostnameConfigured }
                        Text { text: "✓ " + CommonStrings.localeConfigured; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.localeConfigured }
                        Text { text: "✓ " + CommonStrings.userAccountConfigured; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.userConfigured }
                        Text { text: "✓ " + CommonStrings.wifiConfigured; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.wifiConfigured }
                        Text { text: "✓ " + CommonStrings.sshEnabled; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.sshEnabled }
                        Text { text: "✓ " + CommonStrings.piConnectEnabled; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.piConnectEnabled }
                        Text { text: "✓ " + CommonStrings.usbGadgetEnabled; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.featUsbGadgetEnabled }
                        Text { text: "✓ " + CommonStrings.i2cEnabled; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.ifI2cEnabled }
                        Text { text: "✓ " + CommonStrings.spiEnabled; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.ifSpiEnabled }
                        Text { text: "✓ " + CommonStrings.onewireEnabled; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.if1WireEnabled }
                        Text { text: "✓ " + CommonStrings.serialConfigured; font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.ifSerial !== "" && wizardContainer.ifSerial !== "Disabled" }
                    }
                }
                ScrollBar.vertical: ScrollBar { policy: contentItem.implicitHeight > height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff; width: Style.scrollBarWidth }
            }
            Text {
                id: ejectInstruction
                text: root.autoEjectEnabled ? qsTr("The storage device was ejected automatically. You can now remove it safely.") : qsTr("Please eject the storage device before removing it from your computer.")
                font.pixelSize: Style.fontSizeDescription
                font.family: Style.fontFamily
                color: Style.textDescriptionColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Accessible.focusable: true
                focusPolicy: Qt.TabFocus
                activeFocusOnTab: true
            }
        }        
    }
    ]
    
    // Action buttons
    RowLayout {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Style.stepContentMargins
        anchors.rightMargin: Style.stepContentMargins
        anchors.bottomMargin: Style.spacingSmall
        spacing: Style.spacingMedium
        
        Item {
            Layout.fillWidth: true
        }
        
        ImButton {
            id: writeAnotherButton
            text: qsTr("Write Another")
            accessibleDescription: qsTr("Return to storage selection to write the same image to another storage device")
            enabled: true
            activeFocusOnTab: true
            Layout.minimumWidth: Style.buttonWidthMinimum
            Layout.preferredHeight: Style.buttonHeightStandard
            onClicked: {
                // Return to storage selection to write the same image to another SD card
                // This preserves device, OS, and customization settings
                wizardContainer.resetToWriteStep()
            }
        }
        
        ImButtonRed {
            id: finishButton
            text: imageWriter.isEmbeddedMode() ? qsTr("Reboot") : CommonStrings.finish
            accessibleDescription: imageWriter.isEmbeddedMode() ? qsTr("Reboot the system to apply changes") : qsTr("Close Raspberry Pi Imager and exit the application")
            enabled: true
            activeFocusOnTab: true
            Layout.minimumWidth: Style.buttonWidthMinimum
            Layout.preferredHeight: Style.buttonHeightStandard
            onClicked: {
                if (imageWriter.isEmbeddedMode()) {
                    imageWriter.reboot()
                } else {
                    // Close the application
                    // Advanced options settings are already saved
                    Qt.quit()
                }
            }
        }
    }
    
    // Focus management - rebuild when customization visibility changes
    onAnyCustomizationsAppliedChanged: rebuildFocusOrder()
    
    Component.onCompleted: {
        // Register choices section as first focus group
        registerFocusGroup("choices", function() {
            return [choicesHeading, deviceLabel, osLabel, storageLabel]
        }, 0)
        
        // Register customizations section as second focus group
        registerFocusGroup("customizations", function() {
            var items = []
            if (customizationScrollView.visible) {
                items.push(customizationsHeading)
                items.push(customizationScrollView)
            }
            return items
        }, 1)
        
        // Register eject instruction as third focus group
        registerFocusGroup("eject", function() {
            return [ejectInstruction]
        }, 2)
        
        // Register buttons as fourth focus group
        registerFocusGroup("buttons", function() {
            return [writeAnotherButton, finishButton]
        }, 3)
    }

} 
