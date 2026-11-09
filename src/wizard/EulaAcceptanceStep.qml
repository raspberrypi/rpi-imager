/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    objectName: "eulaAcceptanceStep"

    title: qsTr("Licence agreement")
    subtitle: wizardContainer.selectedOsName
    nextButtonEnabled: root.isAccepted
    nextButtonAccessibleDescription: qsTr("Accept the licence and continue to storage selection")
    backButtonAccessibleDescription: qsTr("Return to operating system selection")

    // "fetching", "ready" or "failed"
    property string fetchStatus: "fetching"
    property string licenceText: ""
    property string failureReason: ""
    // This exact text was accepted in an earlier session.
    property bool acceptedBefore: false
    readonly property bool isAccepted: fetchStatus === "ready" && acceptBox.checked

    function fetch() {
        fetchStatus = "fetching"
        failureReason = ""
        acceptBox.checked = false
        ImageWriterSingleton.fetchEula()
    }

    function showLicence(text, accepted) {
        licenceText = text
        acceptedBefore = accepted
        acceptBox.checked = accepted
        fetchStatus = "ready"
        wizardContainer.eulaAccepted = accepted
        rebuildFocusOrder()
    }

    function showFailure(reason) {
        licenceText = ""
        failureReason = reason
        acceptedBefore = false
        acceptBox.checked = false
        fetchStatus = "failed"
        wizardContainer.eulaAccepted = false
        rebuildFocusOrder()
        ImageWriterSingleton.announceToScreenReader(reason)
    }

    // Withdrawing acceptance also withdraws the steps reached on the
    // strength of it, so the sidebar cannot be used to walk past it.
    function recordAcceptance(accepted) {
        ImageWriterSingleton.setEulaAccepted(accepted)
        wizardContainer.eulaAccepted = accepted
        if (!accepted)
            wizardContainer.invalidateStepsFrom(wizardContainer.stepStorageSelection)
    }

    Connections {
        target: ImageWriterSingleton
        function onEulaReady(text, accepted) { root.showLicence(text, accepted) }
        function onEulaFailed(reason) { root.showFailure(reason) }
    }

    Component.onCompleted: {
        root.registerFocusGroup("eula_content", function() {
            var items = []
            var reader = ImageWriterSingleton && ImageWriterSingleton.screenReaderActive
            if (root.fetchStatus === "ready") {
                if (reader) items.push(introText)
                items.push(licenceArea)
                if (reader && acceptedBeforeText.visible) items.push(acceptedBeforeText)
                items.push(acceptBox)
            } else if (root.fetchStatus === "failed") {
                if (reader) items.push(failureText)
                items.push(retryButton)
            } else if (reader) {
                items.push(fetchingText)
            }
            return items
        }, 0)

        root.fetch()
    }

    content: [
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.sectionPadding
        spacing: Style.stepContentSpacing

        // -- Fetching ------------------------------------------------------
        WizardDescriptionText {
            id: fetchingText
            visible: root.fetchStatus === "fetching"
            text: qsTr("Downloading the licence…")
        }

        ProgressBar {
            Layout.fillWidth: true
            visible: root.fetchStatus === "fetching"
            indeterminate: !PlatformHelper.prefersReducedMotion
            Material.accent: Style.formControlActiveColor
            Accessible.role: Accessible.ProgressBar
            Accessible.name: fetchingText.text
        }

        // -- Failed --------------------------------------------------------
        WizardDescriptionText {
            id: failureText
            objectName: "eulaFailureText"
            visible: root.fetchStatus === "failed"
            color: Style.formLabelErrorColor
            text: root.failureReason + "\n\n"
                  + qsTr("The licence must be shown and accepted before this OS can be written.")
            Accessible.role: Accessible.AlertMessage
            Accessible.ignored: !visible
        }

        ImButton {
            id: retryButton
            objectName: "eulaRetryButton"
            visible: root.fetchStatus === "failed"
            text: qsTr("Retry")
            accessibleDescription: qsTr("Try downloading the licence again")
            Layout.alignment: Qt.AlignLeft
            onClicked: root.fetch()
        }

        // -- Ready ---------------------------------------------------------
        WizardDescriptionText {
            id: introText
            visible: root.fetchStatus === "ready"
            text: qsTr("Read the licence for %1. It must be accepted before the OS is written.")
                      .arg(root.wizardContainer.selectedOsName)
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.fetchStatus === "ready"
            color: Style.titleBackgroundColor
            border.color: Style.popupBorderColor
            border.width: 1
            radius: Style.listItemBorderRadius

            ScrollView {
                anchors.fill: parent
                anchors.margins: 1
                clip: true

                // Vendor text: shown exactly as written, never as markup.
                TextArea {
                    id: licenceArea
                    objectName: "eulaText"
                    text: root.licenceText
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    selectByKeyboard: true
                    wrapMode: TextEdit.Wrap
                    font.pointSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                    background: null
                    activeFocusOnTab: true
                    Accessible.role: Accessible.EditableText
                    Accessible.readOnly: true
                    Accessible.name: qsTr("Licence for %1").arg(root.wizardContainer.selectedOsName)
                }
            }
        }

        WizardDescriptionText {
            id: acceptedBeforeText
            objectName: "eulaAcceptedBeforeText"
            visible: root.fetchStatus === "ready" && root.acceptedBefore
            text: qsTr("You have accepted this licence before, and it has not changed since.")
        }

        ImCheckBox {
            id: acceptBox
            objectName: "eulaAcceptCheckBox"
            Layout.fillWidth: true
            visible: root.fetchStatus === "ready"
            text: qsTr("I accept this licence")
            onToggled: root.recordAcceptance(acceptBox.checked)
        }
    }
    ]

    onNextClicked: wizardContainer.eulaAccepted = root.isAccepted
}
