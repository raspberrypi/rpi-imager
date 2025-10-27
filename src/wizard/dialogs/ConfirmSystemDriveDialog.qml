// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import RpiImager
import "../../qmlcomponents"

BaseDialog {
    id: root
    
    // Override positioning for overlayParent support
    required property Item overlayParent
    parent: overlayParent
    anchors.centerIn: parent

    property string driveName: ""
    property string device: ""
    property string sizeStr: ""
    property var mountpoints: []

    signal confirmed()
    signal cancelled()

    readonly property string riskText: CommonStrings.warningRiskText
    readonly property string proceedText: CommonStrings.warningProceedText
    readonly property string systemDriveText: CommonStrings.systemDriveText

    // Custom escape handling
    function escapePressed() {
        root.close()
        root.cancelled()
    }

    // Register focus groups when component is ready
    Component.onCompleted: {
        registerFocusGroup("warning", function(){ 
            return [warningTextElement] 
        }, 0)
        registerFocusGroup("input", function(){ 
            return [driveNameText, nameInput] 
        }, 1)
        registerFocusGroup("buttons", function(){ 
            return [cancelButton, continueButton] 
        }, 2)
    }
    
    onOpened: {
        nameInput.text = ""
        // Let BaseDialog handle the focus management through the focus groups
        // The BaseDialog will automatically set focus to the first focusable item
    }

    // Dialog content
    Text {
        id: warningTextElement
        textFormat: Text.StyledText
        wrapMode: Text.WordWrap
        font.family: Style.fontFamily
        font.pixelSize: Style.fontSizeDescription
        color: Style.textDescriptionColor
        Layout.fillWidth: true
        text: root.riskText + "<br><br>" + root.systemDriveText + "<br><br>" + root.proceedText
        Accessible.role: Accessible.StaticText
        Accessible.name: text.replace(/<[^>]+>/g, '')  // Strip HTML tags for accessibility
        Accessible.ignored: false
        Accessible.focusable: true
        focusPolicy: Qt.TabFocus
        activeFocusOnTab: true
    }

    Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.titleSeparatorColor; Accessible.ignored: true }

    ColumnLayout {
        Layout.fillWidth: true
        Accessible.role: Accessible.Grouping
        Accessible.name: qsTr("Drive information")
        Text { 
            text: qsTr("Size: %1").arg(root.sizeStr)
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        Text {
            text: qsTr("Mounted as: %1").arg(root.mountpoints && root.mountpoints.length > 0 ? root.mountpoints.join(", ") : qsTr("Not mounted"))
            font.family: Style.fontFamily
            color: Style.textDescriptionColor
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Style.titleSeparatorColor; Accessible.ignored: true }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.family: Style.fontFamily
        font.pixelSize: Style.fontSizeDescription
        color: Style.textDescriptionColor
        text: qsTr("To continue, type the exact drive name below:")
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.ignored: false
    }

    Text {
        id: driveNameText
        font.family: Style.fontFamily
        font.bold: true
        color: Style.textDescriptionColor
        text: root.driveName
        // Make this text focusable so VoiceOver can read it
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Drive name to type: %1").arg(text)
        Accessible.ignored: false
        // Make it part of the tab order so it's announced
        focusPolicy: Qt.TabFocus
        activeFocusOnTab: true
    }

    TextField {
        id: nameInput
        Layout.fillWidth: true
        placeholderText: qsTr("Type drive name exactly as shown above")
        text: ""
        activeFocusOnTab: true
        // Combine all information in the name for VoiceOver
        Accessible.name: qsTr("Drive name input. Type exactly: %1. %2").arg(root.driveName).arg(placeholderText)
        Accessible.description: ""
        Keys.onPressed: (event) => {
            if ((event.key === Qt.Key_V && (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) ||
                (event.key === Qt.Key_Insert && (event.modifiers & Qt.ShiftModifier))) {
                event.accepted = true
                return
            }
        }
        onAccepted: {
            if (continueButton.enabled) continueButton.clicked()
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton | Qt.MiddleButton
            onPressed: (mouse) => { mouse.accepted = true }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }

        ImButtonRed {
            id: cancelButton
            text: qsTr("CANCEL")
            accessibleDescription: qsTr("Cancel operation and return to storage selection to choose a different device")
            activeFocusOnTab: true
            onClicked: {
                root.close()
                root.cancelled()
            }
        }

        ImButton {
            id: continueButton
            text: qsTr("CONTINUE")
            accessibleDescription: qsTr("Proceed to write the image to this system drive after confirming the drive name")
            enabled: nameInput.text === root.driveName
            activeFocusOnTab: true
            onClicked: {
                if (!enabled) return
                root.close()
                root.confirmed()
            }
            // Rebuild focus order when enabled state changes
            onEnabledChanged: {
                // Use Qt.callLater to ensure the change is processed
                Qt.callLater(function() {
                    if (root.registerFocusGroup) {
                        // Re-register the buttons focus group to update the focus navigation
                        root.registerFocusGroup("buttons", function(){ 
                            return [cancelButton, continueButton] 
                        }, 1)
                    }
                })
            }
        }
    }

}
