/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../qmlcomponents"
import RpiImager

// Two states served by one dialog:
//
//   1. NeedsInstall  – SMAppService has never been registered for this
//      bundle. The "Continue" button calls the C++ side which invokes
//      SMAppService.register(); macOS shows its native "Allow in
//      Background?" sheet immediately afterwards.
//
//   2. NeedsApproval – Already registered but the user must flip the
//      toggle in System Settings (either they declined the first
//      prompt, or they disabled it later). Offers a deep-link button
//      that opens the Login Items pane.
//
// The owning view binds `state` to ImageWriter.privilegedHelperState
// and shows the dialog when state ∈ { NeedsInstall, NeedsApproval }.
BaseDialog {
    id: root

    property int helperState: 0   // ImageWriter.PrivilegedHelperState enum
    readonly property bool isNeedsInstall:
        helperState === ImageWriter.NeedsInstall
    readonly property bool isNeedsApproval:
        helperState === ImageWriter.NeedsApproval

    function escapePressed() {
        root.reject()
    }

    Component.onCompleted: {
        registerFocusGroup("content", function(){
            return [titleText, descriptionText, hintText]
        }, 0)
        registerFocusGroup("buttons", function(){
            return root.isNeedsApproval
                ? [cancelButton, openSettingsButton]
                : [cancelButton, primaryButton]
        }, 1)
    }

    Text {
        id: titleText
        text: root.isNeedsApproval
              ? qsTr("Allow Raspberry Pi Imager Privileged Helper")
              : qsTr("Privileged Helper required")
        font.pointSize: Style.fontSizeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    Text {
        id: descriptionText
        text: root.isNeedsApproval
              ? qsTr("Raspberry Pi Imager has a privileged helper service "
                     + "that handles writing to your storage device, but "
                     + "macOS is currently blocking it.\n\n"
                     + "Please enable \"Raspberry Pi Imager\" under "
                     + "Allow in the Background, then return to this app.")
              : qsTr("Raspberry Pi Imager uses a privileged helper "
                     + "service to write to storage devices safely. "
                     + "macOS will ask you to allow it in the next step.\n\n"
                     + "You only need to do this once - the helper is "
                     + "automatically launched when needed and exits "
                     + "when you're done.")
        wrapMode: Text.WordWrap
        color: Style.textDescriptionColor
        font.pointSize: Style.fontSizeDescription
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    Text {
        id: hintText
        text: root.isNeedsApproval
              ? qsTr("Look for it under General → Login Items & Extensions → Allow in the Background.")
              : qsTr("The system prompt may appear behind this window.")
        visible: text.length > 0
        wrapMode: Text.WordWrap
        color: Style.textMetadataColor
        font.pointSize: Style.fontSizeSmall
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Style.spacingMedium
        Item { Layout.fillWidth: true }

        ImButton {
            id: cancelButton
            text: qsTr("Cancel")
            accessibleDescription: qsTr("Dismiss this dialog without installing or enabling the helper")
            Layout.preferredWidth: 100
            activeFocusOnTab: true
            onClicked: root.reject()
        }

        ImButtonRed {
            id: openSettingsButton
            visible: root.isNeedsApproval
            text: qsTr("Open System Settings")
            accessibleDescription: qsTr("Open the Login Items pane in System Settings so you can allow the helper")
            Layout.preferredWidth: 200
            activeFocusOnTab: true
            onClicked: {
                if (ImageWriterSingleton) ImageWriterSingleton.openLoginItemsSettings()
                // Don't close - let the user toggle in Settings and
                // return. ImageWriter.privilegedHelperStateChanged will
                // close us when state transitions to Ready.
            }
        }

        ImButtonRed {
            id: primaryButton
            visible: root.isNeedsInstall
            text: qsTr("Continue")
            accessibleDescription: qsTr("Trigger the macOS system prompt to allow the privileged helper")
            Layout.preferredWidth: 130
            activeFocusOnTab: true
            onClicked: {
                root.accept()
            }
        }
    }
}
