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
    
    // Dynamic width based on widest radio button or button row
    // Updates automatically when language/text changes
    implicitWidth: Math.max(
        Math.max(radioOfficial.naturalWidth, radioCustomFile.naturalWidth, radioCustomUri.naturalWidth),
        cancelButton.implicitWidth + saveButton.implicitWidth + Style.spacingMedium * 2
    ) + Style.cardPadding * 4  // Double padding: contentLayout + optionsLayout margins

    // imageWriter is inherited from BaseDialog
    property var wizardContainer: null

    property bool initialized: false
    property url selectedRepo: ""
    property string customRepoUri: ""
    property string originalRepo: ""

    Component.onCompleted: {
        registerFocusGroup("header", function(){
            // Only include header text when screen reader is active (otherwise it's not focusable)
            if (popup.imageWriter && popup.imageWriter.isScreenReaderActive()) {
                return [headerText]
            }
            return []
        }, 0)
        registerFocusGroup("sourceTypes", function(){
            return [radioOfficial, radioCustomFile, radioCustomUri]
        }, 1)
        registerFocusGroup("customFile", function(){
            return radioCustomFile.checked ? [fieldCustomRepository, browseButton] : []
        }, 2)
        registerFocusGroup("customUri", function(){
            return radioCustomUri.checked ? [fieldCustomUri] : []
        }, 3)
        registerFocusGroup("buttons", function(){
            return [cancelButton, saveButton]
        }, 4)
    }

    Connections {
        target: imageWriter
        function onFileSelected(fileUrl) {
            popup.selectedRepo = fileUrl
        }
    }

    // Header
    Text {
        id: headerText
        text: qsTr("Content Repository")
        font.pixelSize: Style.fontSizeLargeHeading
        font.family: Style.fontFamilyBold
        font.bold: true
        color: Style.formLabelColor
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        Accessible.role: Accessible.Heading
        Accessible.name: text + ", " + qsTr("Choose the source for operating system images")
        Accessible.ignored: false
        Accessible.focusable: popup.imageWriter ? popup.imageWriter.isScreenReaderActive() : false
        focusPolicy: (popup.imageWriter && popup.imageWriter.isScreenReaderActive()) ? Qt.TabFocus : Qt.NoFocus
        activeFocusOnTab: popup.imageWriter ? popup.imageWriter.isScreenReaderActive() : false
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
                accessibleDescription: qsTr("Use the official Raspberry Pi operating system repository")
                checked: true
                ButtonGroup.group: repoGroup
                Layout.fillWidth: true  // Enable text wrapping for long translations
            }

            ImRadioButton {
                id: radioCustomFile
                text: qsTr("Use custom file")
                accessibleDescription: qsTr("Load operating system list from a JSON file on your computer")
                checked: false
                ButtonGroup.group: repoGroup
                Layout.fillWidth: true  // Enable text wrapping for long translations
                onCheckedChanged: {
                    if (checked) {
                        Qt.callLater(function() {
                            fieldCustomRepository.forceActiveFocus()
                        })
                    }
                }
            }

            ImRadioButton {
                id: radioCustomUri
                text: qsTr("Use custom URL")
                accessibleDescription: qsTr("Download operating system list from a custom web address")
                checked: false
                ButtonGroup.group: repoGroup
                Layout.fillWidth: true  // Enable text wrapping for long translations
                onCheckedChanged: {
                    if (checked) {
                        Qt.callLater(function() {
                            fieldCustomUri.forceActiveFocus()
                        })
                    }
                }
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
                text: popup.customRepoUri
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
        // Ensure minimum width accommodates buttons
        Layout.minimumWidth: cancelButton.implicitWidth + saveButton.implicitWidth + Style.spacingMedium * 2 + Style.cardPadding
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
                // Allow button to grow to fit text
                implicitWidth: Math.max(Style.buttonWidthMinimum, implicitContentWidth + leftPadding + rightPadding)
                activeFocusOnTab: true
                onClicked: {
                    popup.applySettings();
                    popup.close();
                }
                onEnabledChanged: {
                    // Rebuild focus order when button becomes enabled/disabled
                    Qt.callLater(popup.rebuildFocusOrder)
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
                    originalRepo = selectedRepo.toString()
                } else if (repo.protocol === "http:" || repo.protocol === "https:") {
                    radioOfficial.checked = false
                    radioCustomFile.checked = false
                    radioCustomUri.checked = true
                    customRepoUri = repo.toString()
                    originalRepo = repo.toString()
                } else {
                    radioOfficial.checked = true
                    radioCustomFile.checked = false
                    radioCustomUri.checked = false
                    originalRepo = ""
                }
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
        } else if (radioCustomFile.checked && originalRepo !== selectedRepo.toString()) {
            imageWriter.refreshOsListFrom(selectedRepo)
            // reset wizard to device selection because the repository changed
            wizardContainer.resetWizard()
        } else if (radioCustomUri.checked && originalRepo !== fieldCustomUri.text) {
            imageWriter.refreshOsListFrom(new URL(fieldCustomUri.text))
            // reset wizard to device selection because the repository changed
            wizardContainer.resetWizard()
        }
        initialized = false
    }

    onOpened: {
        initialize()
    }

    property alias repoFileDialog: repoFileDialog

    ImFileDialog {
        id: repoFileDialog
        imageWriter: popup.imageWriter
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
