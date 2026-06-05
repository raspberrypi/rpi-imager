/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root

    title: qsTr("Customisation: Raspberry Pi Connect")
    subtitle: orgModeEnabled
             ? qsTr("Register this device with your Raspberry Pi Connect organisation")
             : qsTr("Sign in to receive a token and enable Raspberry Pi Connect")
    showSkipButton: true
    nextButtonAccessibleDescription: qsTr("Save Raspberry Pi Connect settings and continue to next customisation step")
    backButtonAccessibleDescription: qsTr("Return to previous step")
    skipButtonAccessibleDescription: qsTr("Skip all customisation and proceed directly to writing the image")

    // Only visible/active when the selected OS supports it
    visible: wizardContainer.piConnectAvailable
    enabled: wizardContainer.piConnectAvailable

    // True when "Raspberry Pi Connect for Organisations" is enabled
    // in App Options.  In org mode, this step collects an organisation
    // API key (session-only) instead of a per-user authentication
    // token.  The key is forwarded to the fastboot flash thread and
    // used to register each provisioned device with Connect.
    readonly property bool orgModeEnabled:
        ImageWriterSingleton ? ImageWriterSingleton.getBoolSetting("connect_org_enabled") : false

    // True when the selected target storage is a fastboot device.
    // Determines which organisation flow to run on Next:
    //   true  → device-identity registration via firmware crypto.
    //   false → mint a single-use auth key over the org API key and
    //           write it into the OS image like a per-user token.
    readonly property bool targetIsFastboot:
        wizardContainer ? wizardContainer.targetIsFastboot === true : false

    // Content
    content: [
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Style.sectionPadding
        spacing: Style.stepContentSpacing

        // ── Organisation-mode UI (API key + description prefix) ──
        WizardSectionContainer {
            visible: root.orgModeEnabled

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: Style.fontSizeDescription
                font.family: Style.fontFamily
                color: Style.formLabelColor
                text: root.targetIsFastboot
                    ? qsTr("Each device will join your Connect organisation using its firmware identity key.")
                    : qsTr("A single-use auth key will be written into the image so the device joins your Connect organisation on first boot.")
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.formRowSpacing

                WizardFormLabel {
                    id: labelOrgKey
                    text: qsTr("Organisation API Key:")
                    accessibleDescription: qsTr("Enter or paste your Raspberry Pi Connect organisation API key")
                }

                ImPasswordField {
                    id: fieldOrgApiKey
                    Layout.fillWidth: true
                    placeholderText: root.hasStoredOrgKey
                        ? qsTr("Saved — type to replace")
                        : qsTr("Paste organisation API key")
                    accessibleDescription: qsTr("Raspberry Pi Connect organisation API key. Once saved the value is never redisplayed.")
                    onTextChanged: root.orgKeyDirty = true
                }

                WizardFormLabel {
                    id: labelOrgDesc
                    text: root.targetIsFastboot
                        ? qsTr("Description prefix:")
                        : qsTr("Auth key description:")
                    accessibleDescription: root.targetIsFastboot
                        ? qsTr("Optional prefix for the device description sent to Raspberry Pi Connect. The board type and serial number are appended automatically.")
                        : qsTr("Description for the auth key shown in the Connect organisation UI.")
                }

                ImTextField {
                    id: fieldOrgDescription
                    Layout.fillWidth: true
                    font.pointSize: Style.fontSizeInput
                    placeholderText: qsTr("e.g. Factory-A")
                    text: root.wizardContainer ? root.wizardContainer.connectOrgDescription : ""
                    onTextChanged: {
                        if (root.wizardContainer)
                            root.wizardContainer.connectOrgDescription = text
                    }
                }
            }

            // Visible only when a saved key exists and the user hasn't
            // typed a new one — gives them an out without parroting
            // the placeholder text.
            ImButton {
                id: clearOrgKeyButton
                Layout.alignment: Qt.AlignRight
                visible: root.hasStoredOrgKey && fieldOrgApiKey.text.length === 0
                text: qsTr("Clear saved key")
                accessibleDescription: qsTr("Remove the saved Raspberry Pi Connect organisation API key")
                onClicked: {
                    ImageWriterSingleton.clearConnectOrgRegistration()
                    fieldOrgApiKey.text = ""
                    fieldOrgDescription.text = ""
                    if (root.wizardContainer)
                        root.wizardContainer.connectOrgDescription = ""
                    root.orgKeyDirty = false
                }
            }
        }

        // ── User-token UI (per-user sign-in flow) ──
        WizardSectionContainer {
            visible: !root.orgModeEnabled

            // Enable/disable Connect
            ImOptionPill {
                id: useTokenPill
                Layout.fillWidth: true
                visible: !root.orgModeEnabled
                text: qsTr("Enable Raspberry Pi Connect")
                accessibleDescription: qsTr("Enable secure remote access to your Raspberry Pi through the Raspberry Pi Connect cloud service")
                helpLabel: ImageWriterSingleton.isEmbeddedMode() ? "" : qsTr("What is Raspberry Pi Connect?")
                helpUrl: ImageWriterSingleton.isEmbeddedMode() ? "" : "https://www.raspberrypi.com/software/connect/"
                checked: false
                onToggled: function(isChecked) {
                    root.wizardContainer.piConnectEnabled = isChecked
                    // Rebuild focus order when pill state changes
                    root.rebuildFocusOrder()
                }
            }

            // Request token button (only when enabled and no token present)
            ImButton {
                id: btnOpenConnect
                Layout.fillWidth: true
                text: qsTr("Open Raspberry Pi Connect")
                accessibleDescription: qsTr("Open the Raspberry Pi Connect website in your browser to sign in and receive an authentication token")
                enabled: useTokenPill.checked
                visible: !root.orgModeEnabled && useTokenPill.checked && !root.connectTokenReceived
                onClicked: {
                    if (ImageWriterSingleton) {
                        var authUrl = Qt.resolvedUrl("https://connect.raspberrypi.com/imager/")
                        ImageWriterSingleton.openUrl(authUrl)
                    }
                }
            }

            // Token input field label and field (only when enabled)
            WizardFormLabel {
                id: labelConnectToken
                text: qsTr("Authentication token:")
                visible: !root.orgModeEnabled && useTokenPill.checked
                accessibleDescription: qsTr("Enter or paste the authentication token from Raspberry Pi Connect. The token will be automatically filled if you use the 'Open Raspberry Pi Connect' button to sign in.")
            }

            ImTextField {
                id: fieldConnectToken
                Layout.fillWidth: true
                font.pointSize: Style.fontSizeInput
                visible: !root.orgModeEnabled && useTokenPill.checked
                enabled: root.tokenFieldEnabled
                persistentSelection: true
                mouseSelectionMode: TextInput.SelectCharacters
                placeholderText: {
                    if (root.connectTokenReceived) {
                        return qsTr("Token received from browser")
                    } else if (root.countdownSeconds > 0) {
                        return qsTr("Waiting for token (%1s)").arg(root.countdownSeconds)
                    } else {
                        return qsTr("Paste token here")
                    }
                }
                text: root.connectToken
                onTextChanged: {
                    var token = text.trim()
                    if (token && token.length > 0) {
                        root.connectToken = token
                        countdownTimer.stop()
                        // Don't automatically validate or set as received - that happens on Next click
                    } else {
                        root.connectTokenReceived = false
                        root.connectToken = ""
                    }
                }
            }
        }
    }
    ]

    // Token state and parsing helpers
    property bool connectTokenReceived: false
    property string connectToken: ""
    property int countdownSeconds: 25
    property bool tokenFieldEnabled: false
    property bool tokenFromBrowser: false
    property bool isValid: false

    // Org-mode key state.  A saved key is never re-shown:
    // `hasStoredOrgKey` only controls placeholder text / the Clear
    // row.  `orgKeyDirty` flips as soon as the user types, so we
    // know whether to replace the stored key on Next.
    property bool hasStoredOrgKey:
        ImageWriterSingleton ? ImageWriterSingleton.hasConnectOrgRegistration() : false
    property bool orgKeyDirty: false
    
    // Validation: allow proceed when:
    // - In organisation mode: a key was typed now, or one is already
    //   held for this session.
    // - Otherwise: either Connect is disabled, or Connect is enabled
    //   AND we have a valid token (from browser or typed).
    nextButtonEnabled: {
        if (root.orgModeEnabled) {
            if (fieldOrgApiKey.text.trim().length > 0) {
                return true
            }
            return root.hasStoredOrgKey
        }
        if (!useTokenPill.checked) {
            return true  // Not enabling Connect, can proceed
        }
        // Connect is enabled - need a valid token
        if (root.tokenFromBrowser && root.connectToken.length > 0) {
            return true  // Token received from browser flow (already validated)
        }
        // Manual token entry - check we have something entered
        var token = root.connectToken.trim()
        return token.length > 0
    }
    
    // Countdown timer
    Timer {
        id: countdownTimer
        interval: 1000
        repeat: true
        running: false
        onTriggered: {
            if (root.countdownSeconds > 0) {
                root.countdownSeconds--
            }
            if (root.countdownSeconds === 0) {
                stop()
                root.tokenFieldEnabled = true
                // Rebuild focus order since the text field is now enabled
                root.rebuildFocusOrder()
            }
        }
    }

    Connections {
        target: ImageWriterSingleton

        // Listen for callback with token
        function onConnectTokenReceived(token){
            if (token && token.length > 0) {
                root.connectTokenReceived = true
                root.connectToken = token
                root.tokenFromBrowser = true
                // Stop the countdown but KEEP field disabled forever when from browser
                countdownTimer.stop()
                root.tokenFieldEnabled = false
                // Update the text field when token is received from browser
                if (fieldConnectToken) {
                    fieldConnectToken.text = token
                }
                // Rebuild focus order since the "Open Raspberry Pi Connect" button is now hidden
                root.rebuildFocusOrder()
            }
        }

        // Listen for token cleared signal (when write completes)
        function onConnectTokenCleared() {
            // Reset all Pi Connect customization state
            root.connectTokenReceived = false
            root.connectToken = ""
            root.tokenFromBrowser = false
            root.tokenFieldEnabled = false
            root.countdownSeconds = 25
            countdownTimer.stop()
            useTokenPill.checked = false
            root.wizardContainer.piConnectEnabled = false
            // Clear the text field
            if (fieldConnectToken) {
                fieldConnectToken.text = ""
            }
            // Clear from customization settings
            delete root.wizardContainer.customizationSettings.piConnectEnabled
            // Rebuild focus order
            root.rebuildFocusOrder()
        }
    }

    Component.onCompleted: {
        // Seed the description field from the persisted value (not
        // a secret) so the user can see / edit it.  Skip if the
        // wizard container already has an in-flight value from
        // earlier in the session.
        if (ImageWriterSingleton && wizardContainer &&
            wizardContainer.connectOrgDescription.length === 0) {
            wizardContainer.connectOrgDescription = ImageWriterSingleton.getConnectOrgDescription()
        }

        // When entering this step with a saved key, land on the
        // description field — the API key is the rare case (replace
        // or first-time setup), the description is the everyday tweak.
        // WizardStepBase only seeds initialFocusItem on the first
        // rebuild, so set it explicitly before registering groups.
        if (root.orgModeEnabled && root.hasStoredOrgKey) {
            root.initialFocusItem = fieldOrgDescription
        }

        root.registerFocusGroup("pi_connect", function(){
            if (root.orgModeEnabled) {
                var orgItems = root.hasStoredOrgKey
                    ? [fieldOrgDescription, fieldOrgApiKey.textField]
                    : [fieldOrgApiKey.textField, fieldOrgDescription]
                if (root.hasStoredOrgKey && fieldOrgApiKey.text.length === 0)
                    orgItems.push(clearOrgKeyButton)
                return orgItems
            }
            var items = [useTokenPill.focusItem]
            // Include help link if visible
            if (useTokenPill.helpLinkItem && useTokenPill.helpLinkItem.visible)
                items.push(useTokenPill.helpLinkItem)
            // Only include button if it's actually visible (checked and no token received yet)
            if (useTokenPill.checked && !root.connectTokenReceived)
                items.push(btnOpenConnect)
            // Label is automatically skipped when screen reader is not active (via activeFocusOnTab)
            if (useTokenPill.checked && fieldConnectToken.enabled) {
                items.push(labelConnectToken)
                items.push(fieldConnectToken)
            }
            return items
        }, 0)

        var token = ImageWriterSingleton.getRuntimeConnectToken()
        if (token && token.length > 0) {
            root.connectTokenReceived = true
            root.connectToken = token
            // If token already exists, assume it came from browser (keep disabled)
            root.tokenFromBrowser = true
            root.tokenFieldEnabled = false
            // Update the text field with the existing token
            if (fieldConnectToken) {
                fieldConnectToken.text = token
            }
        }

        // Never load token from persistent settings; token is session-only
        // auto enable if token has already been provided
        if (root.connectTokenReceived) {
            useTokenPill.checked = true
            root.wizardContainer.piConnectEnabled = true
        }
    }
    
    // Start countdown when user clicks "Open Raspberry Pi Connect"
    Connections {
        target: btnOpenConnect
        function onClicked() {
            if (!root.connectTokenReceived && !countdownTimer.running) {
                root.countdownSeconds = 25
                countdownTimer.start()
            }
        }
    }

    onNextClicked: {
        // Reset validation state
        root.isValid = false

        // Organisation mode: push the API key + description to
        // ImageWriter.  The key is persisted in QSettings; the UI
        // never reads it back.
        //
        // Fastboot target → device identity is registered later, in
        // the fastboot pre-reboot hook.  Skip the user-token flow.
        //
        // Non-fastboot target → mint a single-use organisation auth
        // key now and treat it as if the user had pasted a per-user
        // token.  This causes the cloud-init / firstrun customisation
        // to write the key to the image, so the device joins the
        // organisation on first boot.
        if (root.orgModeEnabled) {
            var typedKey = fieldOrgApiKey.text.trim()
            var desc = fieldOrgDescription.text.trim()
            if (typedKey.length > 0) {
                // Replace both key and description.
                ImageWriterSingleton.setConnectOrgRegistration(typedKey, desc)
                fieldOrgApiKey.text = ""  // don't keep the typed secret visible
                root.orgKeyDirty = false
            } else {
                // Keep the existing saved key; only update description.
                ImageWriterSingleton.setConnectOrgDescription(desc)
            }
            // Refresh status for when the user navigates back to this step.
            root.hasStoredOrgKey = ImageWriterSingleton.hasConnectOrgRegistration()
            wizardContainer.connectOrgDescription = desc

            if (!root.targetIsFastboot) {
                // Mint a fresh auth key from the org API key and use it
                // as the Connect token for this image.  requestOrgAuthKey
                // stores the secret in ImageWriter directly — keeps the
                // raw secret off the QML stack and lets the wizard
                // discard it cleanly if the user changes target later.
                var authDesc = desc.length > 0 ? desc : qsTr("Raspberry Pi Imager")
                var authResult = ImageWriterSingleton.requestOrgAuthKey(authDesc, 1)
                if (!authResult || authResult.ok !== true) {
                    authKeyErrorDialog.detail =
                        (authResult && authResult.error) ? authResult.error : ""
                    authKeyErrorDialog.open()
                    return
                }
                wizardContainer.customizationSettings.piConnectEnabled = true
                root.wizardContainer.piConnectEnabled = true
                root.isValid = true
                return
            }

            // Fastboot path: device identity registration runs at
            // flash time, not via cloud-init, so customizationSettings
            // stays clear (the customisation generator must not emit
            // Connect setup into the image).  But the *wizard*-level
            // piConnectEnabled tracks whether the user configured this
            // step at all — it drives the sidebar highlight, the
            // customisation summary, and the "any customisation set"
            // check.  Mark configured iff there's a usable org key
            // the fastboot flash thread will register with.
            delete wizardContainer.customizationSettings.piConnectEnabled
            root.wizardContainer.piConnectEnabled = root.hasStoredOrgKey
            root.isValid = true
            return
        }

        if (useTokenPill.checked) {
            // Validate the token if user entered it manually (not from browser)
            if (!root.tokenFromBrowser) {
                var tokenToValidate = root.connectToken.trim()
                if (!tokenToValidate || tokenToValidate.length === 0) {
                    // No token provided
                    invalidTokenDialog.open()
                    return
                }
                
                // Validate token format
                var tokenIsValid = ImageWriterSingleton.verifyAuthKey(tokenToValidate, true)
                //console.log("Token validation result:", tokenIsValid, "for token:", tokenToValidate)
                
                if (!tokenIsValid) {
                    // Token is invalid
                    invalidTokenDialog.open()
                    return
                }
                // Token is valid, set it in ImageWriterSingleton
                ImageWriterSingleton.overwriteConnectToken(tokenToValidate)
            }
            // Update conserved customization settings (runtime state only - NOT persisted)
            wizardContainer.customizationSettings.piConnectEnabled = true
            root.wizardContainer.piConnectEnabled = true
            root.isValid = true
        } else {
            // Not checked, remove from runtime settings
            delete wizardContainer.customizationSettings.piConnectEnabled
            root.wizardContainer.piConnectEnabled = false
            root.isValid = true
        }
        // Note: piConnectEnabled is intentionally NOT persisted to disk
        // It's a session-only setting tied to the ephemeral Connect token
    }
    
    // Invalid token dialog
    BaseDialog {
        id: invalidTokenDialog
        parent: root.wizardContainer && root.wizardContainer.overlayRootRef ? root.wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        visible: false
        
        // Override height calculation to ensure proper sizing with wrapped text
        height: contentLayout ? Math.max(200, contentLayout.implicitHeight + Style.cardPadding * 2) : 200
        
        function escapePressed() {
            invalidTokenDialog.close()
        }
        
        Component.onCompleted: {
            registerFocusGroup("buttons", function(){ 
                return [okBtn] 
            }, 0)
        }
        
        // Dialog content
        Text {
            id: dialogTitle
            text: qsTr("Invalid Token")
            font.pointSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            // Make accessible to screen readers
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        
        Text {
            id: dialogMessage
            text: qsTr("The token you entered is not valid. Please check the token and try again, or use the 'Open Raspberry Pi Connect' button to get a valid token.")
            font.pointSize: Style.fontSizeFormLabel
            font.family: Style.fontFamily
            color: Style.formLabelColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            // Make accessible to screen readers
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }
            
            ImButton {
                id: okBtn
                text: qsTr("OK")
                accessibleDescription: qsTr("Close this dialog and return to the token field")
                activeFocusOnTab: true
                onClicked: {
                    invalidTokenDialog.close()
                    // Clear the invalid token
                    fieldConnectToken.text = ""
                    root.connectToken = ""
                    root.connectTokenReceived = false
                }
            }
        }
    }

    // Auth-key minting failure dialog (non-fastboot org mode).
    BaseDialog {
        id: authKeyErrorDialog
        parent: root.wizardContainer && root.wizardContainer.overlayRootRef ? root.wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        visible: false

        // Server-supplied message (401 / 422 etc.) shown under the headline.
        property string detail: ""

        height: contentLayout ? Math.max(200, contentLayout.implicitHeight + Style.cardPadding * 2) : 200

        function escapePressed() {
            authKeyErrorDialog.close()
        }

        Component.onCompleted: {
            registerFocusGroup("buttons", function(){
                return [authKeyOkBtn]
            }, 0)
        }

        Text {
            text: qsTr("Could not create auth key")
            font.pointSize: Style.fontSizeHeading
            font.family: Style.fontFamilyBold
            font.bold: true
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Text {
            text: qsTr("Raspberry Pi Imager could not create an organisation auth key. Check that your organisation API key is valid and that this computer is online, then try again.")
            font.pointSize: Style.fontSizeFormLabel
            font.family: Style.fontFamily
            color: Style.formLabelColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Text {
            visible: authKeyErrorDialog.detail.length > 0
            text: authKeyErrorDialog.detail
            // Server-supplied: don't let it render as HTML / detect
            // pseudo-links and become a phishing surface.
            textFormat: Text.PlainText
            font.pointSize: Style.fontSizeFormLabel
            font.family: Style.fontFamily
            color: Style.formLabelErrorColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            Item { Layout.fillWidth: true }

            ImButton {
                id: authKeyOkBtn
                text: qsTr("OK")
                accessibleDescription: qsTr("Close this dialog and return to the organisation API key field")
                activeFocusOnTab: true
                onClicked: authKeyErrorDialog.close()
            }
        }
    }

    // Handle skip button
    onSkipClicked: {
        // Clear all customization flags
        wizardContainer.hostnameConfigured = false
        wizardContainer.localeConfigured = false
        wizardContainer.userConfigured = false
        wizardContainer.wifiConfigured = false
        wizardContainer.sshEnabled = false
        wizardContainer.piConnectEnabled = false

        // Jump to writing step
        wizardContainer.jumpToStep(wizardContainer.stepWriting)
    }
}

