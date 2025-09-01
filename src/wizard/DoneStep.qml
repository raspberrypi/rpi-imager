/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("Write Complete!")
    showBackButton: false
    showNextButton: false
    readonly property bool autoEjectEnabled: imageWriter.getBoolSetting("eject")
    readonly property bool anyCustomizationsApplied: (
        wizardContainer.hostnameConfigured ||
        wizardContainer.localeConfigured ||
        wizardContainer.userConfigured ||
        wizardContainer.wifiConfigured ||
        wizardContainer.sshEnabled ||
        wizardContainer.piConnectEnabled
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
                text: qsTr("Your choices:")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
            }
            
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.spacingSmall
                
                Text { text: qsTr("Device:");                   font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor }
                Text { text: wizardContainer.selectedDeviceName     || qsTr("No device selected"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamilyBold; font.bold: true; color: Style.formLabelColor; Layout.fillWidth: true }
                Text { text: qsTr("Operating System:");         font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor }
                Text { text: wizardContainer.selectedOsName         || qsTr("No image selected"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamilyBold; font.bold: true; color: Style.formLabelColor; Layout.fillWidth: true }
                Text { text: qsTr("Storage Device:");           font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor }
                Text { text: wizardContainer.selectedStorageName    || qsTr("No storage device selected"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamilyBold; font.bold: true; color: Style.formLabelColor; Layout.fillWidth: true }
            }
            
            // Customization summary
            Text {
                text: qsTr("Customizations applied:")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                Layout.topMargin: Style.spacingSmall
                visible: root.anyCustomizationsApplied
            }
            
            Column {
                Layout.fillWidth: true
                spacing: Style.spacingXSmall
                visible: root.anyCustomizationsApplied
                
                Text { text: qsTr("✓ Hostname configured"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.hostnameConfigured }
                Text { text: qsTr("✓ User account configured"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.userConfigured }
                Text { text: qsTr("✓ WiFi configured"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.wifiConfigured }
                Text { text: qsTr("✓ SSH enabled"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.sshEnabled }
                Text { text: qsTr("✓ Pi Connect enabled"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.piConnectEnabled }
                Text { text: qsTr("✓ Locale configured"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.localeConfigured }
            }
            Text { text: root.autoEjectEnabled ? qsTr("The storage device was ejected automatically. You can now remove it safely.") : qsTr("Please eject the storage device before removing it from your computer."); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.textDescriptionColor; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
        }        
    }
    ]
    
    // Action buttons
    RowLayout {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Style.cardPadding
        spacing: Style.spacingMedium
        
        Item {
            Layout.fillWidth: true
        }
        
        ImButton {
            text: qsTr("Write Another")
            enabled: true
            onClicked: {
                // Reset the wizard state to start over
                // Use existing app options settings
                wizardContainer.resetWizard()
            }
        }
        
        ImButtonRed {
            text: imageWriter.isEmbeddedMode() ? qsTr("Reboot") : qsTr("Finish")
            enabled: true
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

} 