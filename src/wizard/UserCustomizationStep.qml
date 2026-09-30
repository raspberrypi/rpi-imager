/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../qmlcomponents"
import "components"
import "dialogs"

import RpiImager

WizardStepBase {
    id: root
    
    property bool hasSavedUserPassword: false
    // Set when a previously saved password was discarded because its hashing
    // algorithm is incompatible with the currently selected OS (drives a hint).
    property bool savedPasswordInvalidated: false
    property string savedUsername: ""

    // The one rule, used by the field's validator and by the check on the
    // restore path. Written once so the two cannot drift: a username that
    // can no longer be typed must not arrive by another route either.
    readonly property var usernameRule: /^[a-z_][a-z0-9_-]*$/

    // Set when a saved username was refused, so the step can say so rather
    // than presenting an empty field with no explanation.
    property string rejectedUsername: ""

    // Quoted back to the user, so it is held to printable characters and a
    // sensible length: a value typed here cannot contain either problem, one
    // written by hand into the settings file can.
    readonly property string rejectedUsernameShown: {
        var out = ""
        for (var i = 0; i < rejectedUsername.length && out.length < 64; ++i) {
            var c = rejectedUsername.charCodeAt(i)
            out += (c < 0x20 || c === 0x7F) ? "?" : rejectedUsername.charAt(i)
        }
        return out + (rejectedUsername.length > out.length ? "\u2026" : "")
    }
    
    title: qsTr("Customisation: Choose username")
    subtitle: qsTr("Create a user account for your Raspberry Pi")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save user account settings and continue to next customisation step")
    backButtonAccessibleDescription: qsTr("Return to previous step")
    skipButtonAccessibleDescription: qsTr("Skip all customisation and proceed directly to writing the image")
    
    // Content
    content: [
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Style.sectionPadding
        spacing: Style.stepContentSpacing
        
        WizardSectionContainer {
            WizardFormGrid {
                Layout.fillWidth: true
                
                WizardFormLabel {
                    id: labelUsername
                    text: qsTr("Username:")
                    accessibleDescription: qsTr("Enter a username for your Raspberry Pi account. The username must be lowercase and contain only letters, numbers, underscores, and hyphens.")
                }
                
                ImTextField {
                    id: fieldUsername
                    objectName: "userNameField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Enter your username")
                    font.pointSize: Style.fontSizeInput
                    trimWhitespace: true

                    validator: RegularExpressionValidator {
                        regularExpression: root.usernameRule
                    }
                }
                
                WizardFormLabel {
                    id: labelPassword
                    text: CommonStrings.password
                    accessibleDescription: root.hasSavedUserPassword ? qsTr("Enter a new password for this account, or leave blank to keep the previously saved password.") : qsTr("Enter a password for this account. You will need to re-enter it in the next field to confirm.")
                }
                
                ImPasswordField {
                    id: fieldPassword
                    objectName: "userPasswordField"
                    Layout.fillWidth: true
                    placeholderText: root.hasSavedUserPassword ? qsTr("Saved (hidden) — leave blank to keep") : qsTr("Enter password")
                    font.pointSize: Style.fontSizeInput
                }
                
                WizardFormLabel {
                    id: labelPasswordConfirm
                    text: qsTr("Confirm password:")
                    accessibleDescription: root.hasSavedUserPassword ? qsTr("Re-enter the new password to confirm, or leave blank to keep the previously saved password.") : qsTr("Re-enter the password to confirm it matches.")
                }
                
                ImPasswordField {
                    id: fieldPasswordConfirm
                    objectName: "userPasswordConfirmField"
                    Layout.fillWidth: true
                    placeholderText: root.hasSavedUserPassword ? qsTr("Re-enter to change password") : qsTr("Re-enter password")
                    font.pointSize: Style.fontSizeInput
                }
            }
            
            WizardDescriptionText {
                id: helpText
                text: qsTr("The username must be lowercase and contain only letters, numbers, underscores, and hyphens.")
            }

            WizardDescriptionText {
                id: rejectedUsernameNotice
                objectName: "usernameRejectedNotice"
                visible: root.rejectedUsername.length > 0
                color: Style.formLabelErrorColor
                text: qsTr("The saved username \u201C%1\u201D is no longer allowed, so the box has been left empty.")
                          .arg(root.rejectedUsernameShown)
                Accessible.role: Accessible.AlertMessage
                Accessible.name: text
                Accessible.ignored: !visible
            }

            WizardDescriptionText {
                id: invalidatedPasswordHint
                visible: root.savedPasswordInvalidated
                color: Style.formLabelErrorColor
                text: qsTr("Your saved password isn't compatible with the selected operating system, so please enter it again.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingSmall
                visible: root.wizardContainer && root.wizardContainer.passwordlessSudoAvailable

                ImCheckBox {
                    id: checkPasswordlessSudo
                    objectName: "passwordlessSudoCheck"
                    text: qsTr("Enable passwordless sudo")
                    checked: false
                    // Carries the risk the dialog explains, because when the
                    // dialog is skipped this is read in its place. Shared with
                    // the info icon below so there is one wording, not two.
                    Accessible.description: sudoInfoIcon.infoText
                    onCheckedChanged: {
                        if (!checked)
                            return
                        // Skipped for the same two reasons as the storage
                        // step's filter warning: the deployment-wide opt-out,
                        // and an assistive technology having already read the
                        // description above aloud. See StorageSelectionStep
                        // for why the second one is the right way round.
                        var warningsOff = root.wizardContainer
                                       && root.wizardContainer.disableWarnings
                        if (ConfirmationPolicy.shouldConfirm(warningsOff))
                            passwordlessSudoWarningDialog.askForConfirmation()
                    }
                }

                Text {
                    id: sudoInfoIcon
                    readonly property string infoText: qsTr("Allows any process running as this user to gain full root privileges without a password. Only enable this if you have a specific need, such as automated scripts or headless operation.")

                    text: "ⓘ"
                    font.pointSize: Style.fontSizeFormLabel
                    color: sudoInfoArea.containsMouse || activeFocus ? Style.textDescriptionColor : Style.textMetadataColor
                    Layout.alignment: Qt.AlignVCenter

                    activeFocusOnTab: ImageWriterSingleton ? ImageWriterSingleton.screenReaderActive : false
                    focusPolicy: (ImageWriterSingleton && ImageWriterSingleton.screenReaderActive) ? Qt.TabFocus : Qt.NoFocus

                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Passwordless sudo information: ") + infoText

                    ToolTip.text: infoText
                    ToolTip.visible: sudoInfoArea.containsMouse || activeFocus

                    MouseArea {
                        id: sudoInfoArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.WhatsThisCursor
                        acceptedButtons: Qt.NoButton
                    }
                }
            }
        }
    }
    ]

    PasswordlessSudoWarningDialog {
        id: passwordlessSudoWarningDialog
        objectName: "passwordlessSudoWarning"
        parent: root.wizardContainer && root.wizardContainer.overlayRootRef ? root.wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        visible: false

        onCancelled: {
            checkPasswordlessSudo.checked = false
        }
    }

    Component.onCompleted: {
        // Include labels before their corresponding fields so users hear the explanation first
        root.registerFocusGroup("user_fields", function(){
            // Labels are automatically skipped when screen reader is not active (via activeFocusOnTab)
            return [labelUsername, fieldUsername, labelPassword, fieldPassword, labelPasswordConfirm, fieldPasswordConfirm, helpText, checkPasswordlessSudo, sudoInfoIcon]
        }, 0)
        
        // Set initial focus on the username field
        // Initial focus will automatically go to title, then subtitle, then first control (handled by WizardStepBase)
        
        // Prefill from conserved customization settings (avoid showing raw passwords)
        var settings = wizardContainer.customizationSettings
        if (settings.sshUserName) {
            // Checked rather than trusted. Assigning text does not run the
            // validator -- Qt only clears acceptableInput, which nothing
            // here reads -- so a username saved by an older version, edited
            // into the settings file, or left behind by a tightening of the
            // rules came back and was written out again on Next. It becomes
            // the account name in firstrun.sh, which runs as root.
            if (root.usernameRule.test(settings.sshUserName)) {
                fieldUsername.text = settings.sshUserName
                root.savedUsername = settings.sshUserName
            } else {
                root.rejectedUsername = settings.sshUserName
            }
        }
        if (settings.sshUserPassword) {
            if (ImageWriterSingleton.savedUserPasswordUsableWithCurrentOs(settings.sshUserPassword)) {
                // Indicate a saved (crypted) password exists; do not prefill fields.
                // Because the field stays empty, the show-password toggle is
                // unavailable for restored passwords (we only hold the hash) - it is
                // only available for passwords typed in the current session.
                root.hasSavedUserPassword = true
            } else {
                // The saved hash uses yescrypt but the selected OS predates
                // yescrypt support, so it could never authenticate. We hold no
                // plaintext to re-hash, so drop the stale hash and require
                // re-entry; re-hashing under this OS yields sha256crypt, which is
                // accepted everywhere.
                delete wizardContainer.customizationSettings.sshUserPassword
                ImageWriterSingleton.removePersistedCustomisationSetting("sshUserPassword")
                root.hasSavedUserPassword = false
                root.savedPasswordInvalidated = true
            }
        }
        // Note: passwordlessSudo is intentionally NOT loaded from persisted settings.
        // It must be explicitly enabled each session due to security implications.
    }
    
    // Validation
    // Allow proceed when:
    // - all fields are empty (no customization), or
    // - a new password is entered and matches confirm, or
    // - a saved (crypted) password exists and user leaves password fields blank (with or without username)
    nextButtonEnabled: (
        (fieldUsername.text.length === 0 && fieldPassword.text.length === 0 && fieldPasswordConfirm.text.length === 0)
        || (fieldPassword.text.length > 0 && fieldPasswordConfirm.text.length > 0 && fieldPassword.text === fieldPasswordConfirm.text)
        || (root.hasSavedUserPassword && fieldPassword.text.length === 0 && fieldPasswordConfirm.text.length === 0)
    )
    
    // Save settings when moving to next step
    onNextClicked: {
        var usernameText = fieldUsername.value
        var hasPasswords = fieldPassword.text.length > 0 && fieldPassword.text === fieldPasswordConfirm.text
        
        // Update conserved customization settings (runtime state)
        if (usernameText.length > 0 && hasPasswords) {
            // User entered both username and a new password. Hash it now (in C++/
            // generator) and keep ONLY the hash: the plaintext is never copied
            // into the long-lived settings map. The plaintext stays solely in the
            // password field, which is destroyed on navigation - this bounds its
            // RAM lifetime and is what the show-password toggle relies on.
            var hashedPwd = ImageWriterSingleton.hashUserPassword(fieldPassword.text)
            wizardContainer.customizationSettings.sshUserName = usernameText
            wizardContainer.customizationSettings.sshUserPassword = hashedPwd
            wizardContainer.userConfigured = true
            // Persist for future sessions (stored hashed, never as plaintext)
            ImageWriterSingleton.setPersistedCustomisationSetting("sshUserName", usernameText)
            ImageWriterSingleton.setPersistedCustomisationSetting("sshUserPassword", hashedPwd)
            // Passwordless sudo (session-only, never persisted)
            if (checkPasswordlessSudo.checked) {
                wizardContainer.customizationSettings.passwordlessSudo = true
            } else {
                delete wizardContainer.customizationSettings.passwordlessSudo
            }
        } else if (usernameText.length > 0 && root.hasSavedUserPassword && fieldPassword.text.length === 0) {
            // User has a username (entered or kept from saved) and saved password (keeping it)
            wizardContainer.customizationSettings.sshUserName = usernameText
            // Keep existing sshUserPassword in runtime settings
            wizardContainer.userConfigured = true
            // Persist username (password already persisted)
            ImageWriterSingleton.setPersistedCustomisationSetting("sshUserName", usernameText)
            // Passwordless sudo (session-only, never persisted)
            if (checkPasswordlessSudo.checked) {
                wizardContainer.customizationSettings.passwordlessSudo = true
            } else {
                delete wizardContainer.customizationSettings.passwordlessSudo
            }
        } else if (usernameText.length === 0 && fieldPassword.text.length === 0 && fieldPasswordConfirm.text.length === 0) {
            // User cleared all fields -> remove from runtime settings
            delete wizardContainer.customizationSettings.sshUserName
            delete wizardContainer.customizationSettings.sshUserPassword
            delete wizardContainer.customizationSettings.sshUserPasswordPlain
            delete wizardContainer.customizationSettings.passwordlessSudo
            wizardContainer.userConfigured = false
            // Remove from persistence
            ImageWriterSingleton.removePersistedCustomisationSetting("sshUserName")
            ImageWriterSingleton.removePersistedCustomisationSetting("sshUserPassword")
        } else {
            // Partial/invalid -> do not mark configured
            wizardContainer.userConfigured = false
        }
        // Do not log sensitive data
    }
    
    // Handle skip button
    // Skipping means skipping all of it, wherever the button is pressed.
    onSkipClicked: wizardContainer.skipAllCustomisation()
} 
