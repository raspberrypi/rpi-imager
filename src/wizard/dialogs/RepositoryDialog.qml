/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../../qmlcomponents"

import RpiImager

BaseDialog {
    id: popup

    required property ImageWriter imageWriter
    property var wizardContainer: null

    property bool initialized: false
    property url selectedRepo: ""
    property string customRepoUri: ""
    property url originalRepo: ""

    Component.onCompleted: {
        registerFocusGroup("sourceTypes", function(){
            return [radioOfficial, radioCustomFile, radioCustomUri]
        }, 0)
        registerFocusGroup("customFile", function(){
            return radioCustomFile.checked ? [fieldCustomRepository, browseButton] : []
        }, 1)
        registerFocusGroup("customUri", function(){
            return radioCustomUri.checked ? [fieldCustomUri] : []
        }, 2)
        registerFocusGroup("buttons", function(){
            return [cancelButton, saveButton]
        }, 3)
    }

    Connections {
        target: imageWriter
        function onFileSelected(fileUrl) {
            popup.selectedRepo = fileUrl
        }
    }

    // Header
    Text {
        text: qsTr("Content Repository")
        font.pixelSize: Style.fontSizeLargeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
    }

    // Options section
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: optionsLayout.implicitHeight + Style.cardPadding

        ColumnLayout {
            id: optionsLayout
            anchors.fill: parent
            anchors.margins: Style.cardPadding
            spacing: Style.spacingMedium

            WizardFormLabel {
                text: qsTr("Repository source:")
            }

            ButtonGroup { id: repoGroup }

            ImRadioButton {
                id: radioOfficial
                text: "Raspberry Pi (default)"
                checked: true
                ButtonGroup.group: repoGroup
            }

            ImRadioButton {
                id: radioCustomFile
                text: qsTr("Use custom file")
                checked: false
                ButtonGroup.group: repoGroup
            }

            ImRadioButton {
                id: radioCustomUri
                text: qsTr("Use custom URL")
                checked: false
                ButtonGroup.group: repoGroup
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                visible: radioCustomFile.checked

                ImTextField {
                    id: fieldCustomRepository
                    text: selectedRepo !== "" ? UrlFmt.display(selectedRepo) : ""
                    Layout.fillWidth: true
                    placeholderText: qsTr("Please select a custom repository json file")
                    font.pixelSize: Style.fontSizeInput
                    readOnly: true
                    activeFocusOnTab: true
                }

                ImButton {
                    id: browseButton
                    text: CommonStrings.browse
                    accessibleDescription: qsTr("Select a custom repository JSON file from your computer")
                    Layout.minimumWidth: 80
                    activeFocusOnTab: true
                    onClicked: {
                        // Prefer native file dialog via Imager's wrapper, but only if available
                        if (imageWriter.nativeFileDialogAvailable()) {
                            // Defer opening the native dialog until after the current event completes
                            Qt.callLater(function () {
                                imageWriter.openFileDialog(qsTr("Select Repository"), CommonStrings.repoFiltersString);
                            });
                        } else {
                            // Fallback to QML dialog (forced non-native)
                            repoFileDialog.open();
                        }
                    }
                }
            }

            ImTextField {
                id: fieldCustomUri
                visible: radioCustomUri.checked
                Layout.fillWidth: true
                text: customRepoUri
                placeholderText: "https://path.to.my/repo.json"
                font.pixelSize: Style.fontSizeInput
                activeFocusOnTab: true
                inputMethodHints: Qt.ImhUrlCharactersOnly

                property bool isValid: fieldCustomUri.isStrictRepoUrl(text)

                function isStrictRepoUrl(s) {
                    // http/https, at least one non-space char, ends with .json
                    return /^https?:\/\/[^ \t\r\n]+\.json$/i.test(s)
                }
            }
        }
    }

    // Spacer
    Item {
        Layout.fillHeight: true
    }

    // Buttons section with background
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: buttonRow.implicitHeight + Style.cardPadding
        color: Style.titleBackgroundColor

        RowLayout {
            id: buttonRow
            anchors.fill: parent
            anchors.margins: Style.cardPadding / 2
            spacing: Style.spacingMedium

            Item {
                Layout.fillWidth: true
            }

            ImButton {
                id: cancelButton
                text: CommonStrings.cancel
                accessibleDescription: qsTr("Close the repository dialog without changing the content source")
                Layout.minimumWidth: Style.buttonWidthMinimum
                activeFocusOnTab: true
                onClicked: {
                    popup.initialized = false
                    popup.close();
                }
            }

            ImButtonRed {
                id: saveButton
                enabled: (radioOfficial.checked
                         || (radioCustomFile.checked && popup.selectedRepo.toString() !== "")
                         || (radioCustomUri.checked && fieldCustomUri.isValid))
                         // Disable while write is in progress to prevent restarting during write
                         && (imageWriter.writeState === ImageWriter.Idle ||
                             imageWriter.writeState === ImageWriter.Succeeded ||
                             imageWriter.writeState === ImageWriter.Failed ||
                             imageWriter.writeState === ImageWriter.Cancelled)
                // TODO: only show or enable when settings changed
                text: qsTr("Apply & Restart")
                accessibleDescription: qsTr("Apply the new content repository and restart the wizard from the beginning")
                Layout.minimumWidth: Style.buttonWidthMinimum
                activeFocusOnTab: true
                onClicked: {
                    popup.applySettings();
                    popup.close();
                }
            }
        }
    }

    Connections {
        target: repoGroup
        function onCheckedButtonChanged() {
            popup.rebuildFocusOrder()
        }
    }

    function initialize() {
        if (!initialized) {
            initialized = true

            if (imageWriter.customRepo()) {
                var repo = new URL(imageWriter.osListUrl())
                if (repo.protocol === "file:") {
                    radioOfficial.checked = false
                    radioCustomFile.checked = true
                    radioCustomUri.checked = false
                    selectedRepo = repo
                } else if (repo.protocol === "http:" || repo.protocol === "https:") {
                    radioOfficial.checked = false
                    radioCustomFile.checked = false
                    radioCustomUri.checked = true
                    customRepoUri = repo.toString()
                } else {
                    radioOfficial.checked = true
                    radioCustomFile.checked = false
                    radioCustomUri.checked = false
                }

                originalRepo = selectedRepo
            } else {
                radioOfficial.checked = true
                radioCustomFile.checked = false
                radioCustomUri.checked = false
                selectedRepo = ""
                customRepoUri = ""
                originalRepo = ""
            }

            // Ensure focus order is built after initial state
            popup.rebuildFocusOrder()
        }
    }

    function applySettings() {
        // Save settings to ImageWriter
        // Only save repository setting if it has actually changed
        if (radioOfficial.checked && originalRepo !== "") {
            imageWriter.refreshOsListFromDefaultUrl()
            // reset wizard to device selection because the repository changed
            wizardContainer.resetWizard()
        } else if (radioCustomFile.checked && originalRepo !== selectedRepo) {
            imageWriter.refreshOsListFrom(selectedRepo)
            // reset wizard to device selection because the repository changed
            wizardContainer.resetWizard()
        } else if (radioCustomUri.checked && originalRepo.toString() !== customRepoUri) {
            imageWriter.refreshOsListFrom(new URL(fieldCustomUri.text))
            // reset wizard to device selection because the repository changed
            wizardContainer.resetWizard()
        }
    }

    onOpened: {
        initialize()
    }

    property alias repoFileDialog: repoFileDialog

    ImFileDialog {
        id: repoFileDialog
        dialogTitle: qsTr("Select custom repository")
        nameFilters: CommonStrings.repoFiltersList
        onAccepted: {
            popup.selectedRepo = selectedFile;
        }
        onRejected:
        // No-op; user cancelled
        {}
    }
}
