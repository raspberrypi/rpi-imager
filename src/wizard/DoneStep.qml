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
    subtitle: qsTr("Your Raspberry Pi is ready to use")
    showBackButton: false
    showNextButton: false
    
    // Content
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        spacing: 20
        
        // Success message
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: successLayout.implicitHeight + 40
            Layout.maximumWidth: 500
            Layout.alignment: Qt.AlignHCenter
            color: Style.titleBackgroundColor
            border.color: Style.progressBarVerifyForegroundColor
            border.width: 2
            radius: 8
            
            ColumnLayout {
                id: successLayout
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15
                
                Text {
                    text: qsTr("✅ Success!")
                    font.pixelSize: 24
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.progressBarVerifyForegroundColor
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
                
                Text {
                    text: qsTr("Your Raspberry Pi image has been successfully written to the storage device.")
                    font.pixelSize: 14
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
        
        // What was configured
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: configuredLayout.implicitHeight + 40
            color: Style.titleBackgroundColor
            border.color: Style.titleSeparatorColor
            border.width: 1
            radius: 8
            
            ColumnLayout {
                id: configuredLayout
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15
                
                Text {
                    text: qsTr("What was configured:")
                    font.pixelSize: 16
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
                
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 20
                    rowSpacing: 8
                    
                    Text {
                        text: qsTr("Device:")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                    }
                    
                    Text {
                        text: wizardContainer.selectedDeviceName || qsTr("No device selected")
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        color: Style.formLabelColor
                        Layout.fillWidth: true
                    }
                    
                    Text {
                        text: qsTr("Operating System:")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                    }
                    
                    Text {
                        text: wizardContainer.selectedOsName || qsTr("No image selected")
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        color: Style.formLabelColor
                        Layout.fillWidth: true
                    }
                    
                    Text {
                        text: qsTr("Storage:")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                    }
                    
                    Text {
                        text: wizardContainer.selectedStorageName || qsTr("No storage selected")
                        font.pixelSize: 12
                        font.family: Style.fontFamilyBold
                        font.bold: true
                        color: Style.formLabelColor
                        Layout.fillWidth: true
                    }
                }
                
                // Customization summary
                Text {
                    text: qsTr("Customizations applied:")
                    font.pixelSize: 14
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                }
                
                Column {
                    Layout.fillWidth: true
                    spacing: 5
                    
                    Text {
                        text: qsTr("✓ Hostname configured")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                        visible: wizardContainer.hostnameConfigured
                    }
                    
                    Text {
                        text: qsTr("✓ User account configured")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                        visible: wizardContainer.userConfigured
                    }
                    
                    Text {
                        text: qsTr("✓ WiFi configured")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                        visible: wizardContainer.wifiConfigured
                    }
                    
                    Text {
                        text: qsTr("✓ SSH enabled")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                        visible: wizardContainer.sshEnabled
                    }
                    
                    Text {
                        text: qsTr("✓ Pi Connect enabled")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                        visible: wizardContainer.piConnectEnabled
                    }
                    
                    Text {
                        text: qsTr("✓ Locale configured")
                        font.pixelSize: 12
                        font.family: Style.fontFamily
                        color: Style.formLabelColor
                        visible: wizardContainer.localeConfigured
                    }
                }
            }
        }
        
    }
    
    // Action buttons
    RowLayout {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20
        spacing: 15
        
        Item {
            Layout.fillWidth: true
        }
        
        ImButton {
            text: qsTr("Write Another")
            enabled: true
            onClicked: {
                // Reset the wizard state to start over
                // Use existing advanced options settings
                wizardContainer.resetWizard()
            }
        }
        
        ImButton {
            text: qsTr("Finish")
            enabled: true
            onClicked: {
                // Close the application
                // Advanced options settings are already saved
                Qt.quit()
            }
        }
    }

} 