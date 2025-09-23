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
            wizardContainer.piConnectEnabled
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
                text: qsTr("Customisations applied:")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                Layout.topMargin: Style.spacingSmall
                visible: root.anyCustomizationsApplied
            }
            
            ScrollView {
                id: customizationScrollView
                Layout.fillWidth: true
                Layout.maximumHeight: Math.round(root.height * 0.4)
                clip: true
                visible: root.anyCustomizationsApplied
                activeFocusOnTab: true
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
                        spacing: Style.spacingXSmall
                        Text { text: qsTr("✓ Hostname configured"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.hostnameConfigured }
                        Text { text: qsTr("✓ User account configured"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.userConfigured }
                        Text { text: qsTr("✓ Wi‑Fi configured"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.wifiConfigured }
                        Text { text: qsTr("✓ SSH enabled"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.sshEnabled }
                        Text { text: qsTr("✓ Raspberry Pi Connect enabled"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.piConnectEnabled }
                        Text { text: qsTr("✓ Locale configured"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.localeConfigured }
                    }
                }
                ScrollBar.vertical: ScrollBar { policy: contentItem.implicitHeight > height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff; width: Style.scrollBarWidth }
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
            enabled: true
            activeFocusOnTab: true
            Layout.minimumWidth: Style.buttonWidthMinimum
            Layout.preferredHeight: Style.buttonHeightStandard
            onClicked: {
                // Reset the wizard state to start over
                // Use existing app options settings
                wizardContainer.resetWizard()
            }
        }
        
        ImButtonRed {
            id: finishButton
            text: imageWriter.isEmbeddedMode() ? qsTr("Reboot") : qsTr("Finish")
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
        registerFocusGroup("scrollview", function() {
            // Only include the scroll view when it's visible and has content to scroll
            if (customizationScrollView.visible && customizationScrollView.contentItem.contentHeight > customizationScrollView.height) {
                return [customizationScrollView]
            }
            return []
        }, 0)
        
        registerFocusGroup("buttons", function() {
            return [writeAnotherButton, finishButton]
        }, 1)
    }

} 