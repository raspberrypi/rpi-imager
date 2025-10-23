/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls
import "../qmlcomponents"
import "components"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    // "open" | "secure"
    property string wifiMode: "secure"
    property string originalSavedSSID: ""
    property bool hadSavedCrypt: false
    property bool showPw: wifiMode === "secure"
    property bool confirmNeedsInput: false

    function ssidUnchanged(ssid, prev) { return (ssid || "") === (prev || "") }
    
    title: qsTr("Customisation: Choose Wi‑Fi")
    showSkipButton: true
    
    // Set initial focus on SSID
    // don't default to the mode switchers so they aren't tempted to select open ;)
    initialFocusItem: fieldWifiSSID

    Component.onCompleted: {
        root.registerFocusGroup("wifi_modes", function() {
            return [tabSecure, tabOpen]
        }, 0)
        root.registerFocusGroup("wifi_fields", function(){
            var items = [fieldWifiSSID]
            if (showPw) {
                items.push(fieldWifiPassword)
                if (confirmNeedsInput)
                    items.push(fieldWifiPasswordConfirm)
            }
            return items
        }, 1)
        root.registerFocusGroup("wifi_options", function(){ return [chkWifiHidden] }, 2)

        // Set SSID placeholder before prefilling text content
        fieldWifiSSID.placeholderText = qsTr("Network name")

        // Prefill from saved settings
        var saved = imageWriter.getSavedCustomizationSettings()

        // Set SSID placeholder first (before setting any text)
        fieldWifiSSID.placeholderText = qsTr("Network name")

        // Then set text values after, so they properly override the placeholder
        if (saved.wifiSSID) {
            fieldWifiSSID.text = saved.wifiSSID
        }

        // If not saved, try to auto-detect the current SSID from the system
        if (!fieldWifiSSID.text || fieldWifiSSID.text.length === 0) {
            var detectedSsid = imageWriter.getSSID()
            console.log("WifiCustomizationStep: detected SSID:", detectedSsid)
            if (detectedSsid && detectedSsid.length > 0) {
                fieldWifiSSID.text = detectedSsid
            }
        }
        if (saved.wifiHidden !== undefined) {
            chkWifiHidden.checked = (saved.wifiHidden === true || saved.wifiHidden === "true")
        }

        originalSavedSSID = saved.wifiSSID || ""
        // Remember if a crypted PSK is already saved (affects placeholder/keep semantics)
        hadSavedCrypt = !!saved.wifiPasswordCrypt

        // if no saved crypt, try to prefill a PSK from system
        // IMPORTANT: Only attempt PSK retrieval if we have an SSID (either saved or detected)
        // Pass the SSID to getPSKForSSID() to avoid race condition where SSID detection
        // might fail during the keychain permission dialog on macOS
        if (!hadSavedCrypt && fieldWifiSSID.text && fieldWifiSSID.text.length > 0) {
            // Auto-populate WiFi password from system keychain when available
            // Only when no crypted password is already saved
            var psk = imageWriter.getPSKForSSID(fieldWifiSSID.text)
            if (psk && psk.length > 0) {
                fieldWifiPassword.text = psk
                fieldWifiPasswordConfirm.text = psk
            }
        }

        // wifiMode: prefer saved value; otherwise infer from whether a password is present
        wifiMode = (saved.wifiMode === "secure" || saved.wifiMode === "open")
            ? saved.wifiMode
            : "secure"

        updatePasswordFieldUI()
        // UpdatePasswordFieldUI already takes care of this
        //root.rebuildFocusOrder()
    }

    function updatePasswordFieldUI() {
        var ssid = (fieldWifiSSID.text || "").trim()
        var prevSSID = originalSavedSSID

        if (wifiMode === "open") {
            fieldWifiPassword.text = ""
            fieldWifiPassword.enabled = false
            fieldWifiPassword.placeholderText = qsTr("No password (open network)")

            fieldWifiPasswordConfirm.text = ""
            fieldWifiPasswordConfirm.enabled = false
            confirmNeedsInput = false
            root.rebuildFocusOrder()
            return
        }

        // secure
        fieldWifiPassword.enabled = true
        var canKeep = hadSavedCrypt && ssidUnchanged(ssid, prevSSID)
        fieldWifiPassword.placeholderText = canKeep
           ? qsTr("Saved (hidden) — leave blank to keep")
           : qsTr("Network password")

        var userTypedNew = (fieldWifiPassword.text && fieldWifiPassword.text.length > 0)
        confirmNeedsInput = (!canKeep) || userTypedNew
        fieldWifiPasswordConfirm.enabled = confirmNeedsInput
        root.rebuildFocusOrder()
    }

    function passwordErrorMessage() {
        if (!showPw) return " ";

        // Gather state
        var ssidNow = (fieldWifiSSID.text || "").trim();
        var canKeep = hadSavedCrypt && ssidUnchanged(ssidNow, originalSavedSSID);
        var pwd = fieldWifiPassword.text || "";
        var conf = fieldWifiPasswordConfirm.text || "";

        // Open mode: no errors
        if (wifiMode === "open") return " ";

        // If we can keep the saved crypt and user left blank => OK
        if (canKeep && pwd.length === 0) return " ";

        // New/changed password is required from here
        // Empty -> prompt explicitly
        if (pwd.length === 0) return qsTr("Enter a password");

        // Detailed validity (mirrors isValidWifiPassword)
        // 64 hex is allowed => if it's 64 chars but not hex, say invalid chars
        var isHex = isHex64(pwd);
        if (!isHex) {
            if (pwd.length < 8) return qsTr("Password is too short (min 8 characters)");
            if (pwd.length > 63) return qsTr("Password is too long (max 63 characters)");
            if (!isAsciiPrintable(pwd)) return qsTr("Password contains unsupported characters");
        }

        // If confirm is required, enforce match
        if (confirmNeedsInput && (conf.length > 0 || pwd.length > 0) && pwd !== conf)
            return qsTr("Passwords don’t match");

        // No errors -> keep row height stable
        return " ";
    }

    // Content
    content: [
    ScrollView {
        id: wifiScroll
        anchors.fill: parent
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        // Smoothly bring a child item into view (vertical)
        function scrollToItem(item, margin) {
            if (!item || !wifiScroll.contentItem) return;
            var flick = wifiScroll.contentItem;              // QQuickFlickable
            var content = flick.contentItem;                 // the inner Item holding children
            if (!content) return;

            var pos = item.mapToItem(content, 0, 0);         // item position in content coords
            var top = pos.y;
            var bottom = top + item.height;
            var m = (margin === undefined ? 12 : margin);

            var viewTop = flick.contentY;
            var viewBottom = viewTop + flick.height;

            if (top < viewTop + m) {
                flick.contentY = Math.max(0, top - m);
            } else if (bottom > viewBottom - m) {
                var target = bottom - flick.height + m;
                flick.contentY = Math.min(target, flick.contentHeight - flick.height);
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Style.sectionPadding
            spacing: Style.stepContentSpacing
            width: wifiScroll.availableWidth

            WizardSectionContainer {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingSmall

                    ImToggleTab {
                        id: tabSecure
                        text: qsTr("Secure network")
                        accessibleDescription: qsTr("Configure Wi-Fi for a password-protected network with WPA2/WPA3 encryption")
                        active: wifiMode === "secure"
                        onClicked: { wifiMode = "secure"; updatePasswordFieldUI() }

                        onActiveFocusChanged: {
                            if (activeFocus) wifiScroll.scrollToItem(this);
                        }
                    }

                    ImToggleTab {
                        id: tabOpen
                        text: qsTr("Open network")
                        accessibleDescription: qsTr("Configure Wi-Fi for an unencrypted network without password protection")
                        active: wifiMode === "open"
                        onClicked: { wifiMode = "open"; updatePasswordFieldUI() }

                        onActiveFocusChanged: {
                            if (activeFocus) wifiScroll.scrollToItem(this);
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // No explicit enable checkbox; intent is inferred from inputs
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Style.formColumnSpacing
                    rowSpacing: Style.formRowSpacing

                    WizardFormLabel {
                        text: qsTr("SSID:")
                    }

                    ImTextField {
                        id: fieldWifiSSID
                        Layout.fillWidth: true
                        font.pixelSize: Style.fontSizeInput
                        onTextChanged: updatePasswordFieldUI()
                        onActiveFocusChanged: {
                            if (activeFocus)
                                wifiScroll.scrollToItem(this);
                        }
                    }

                    WizardFormLabel {
                        id: lblPassword
                        text: CommonStrings.password
                        visible: showPw
                    }

                    ImTextField {
                        id: fieldWifiPassword
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        font.pixelSize: Style.fontSizeInput
                        visible: showPw

                        onActiveFocusChanged: {
                            if (activeFocus)
                                wifiScroll.scrollToItem(this);
                        }
                        onTextChanged: {
                            updatePasswordFieldUI()
                            wifiScroll.scrollToItem(fieldWifiPassword)
                        }
                    }

                    /* Confirm password row */
                    WizardFormLabel {
                        id: lblPasswordConfirm
                        text: qsTr("Confirm password:")
                        visible: showPw
                    }

                    ImTextField {
                        id: fieldWifiPasswordConfirm
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        font.pixelSize: Style.fontSizeInput
                        placeholderText: qsTr("Re-enter password")
                        visible: showPw
                        enabled: confirmNeedsInput
                        onActiveFocusChanged: {
                            if (activeFocus)
                                wifiScroll.scrollToItem(this);
                        }
                        onTextChanged: {
                            // keep scroll behavior pleasant while typing
                            wifiScroll.scrollToItem(fieldWifiPasswordConfirm);
                        }
                    }

                    // Empty label to maintain grid alignment
                    Item { width: 1; height: 1; visible: showPw }

                    Text {
                        id: pwdHint
                        Layout.fillWidth: true
                        Layout.columnSpan: 1
                        visible: showPw
                        wrapMode: Text.WordWrap
                        text: passwordErrorMessage()
                        color: (text === " ") ? "transparent" : Style.formLabelErrorColor
                        //font.pixelSize: Style.fontSizeFormLabel
                        font.pixelSize: 11

                        // lock a minimum height so even " " keeps the same line height
                        // TextMetrics is lighter than FontMetrics in Controls:
                        TextMetrics { id: pwdHintMetrics; font: pwdHint.font; text: "X" }
                        Layout.preferredHeight: pwdHintMetrics.height
                    }

                    ImCheckBox {
                        id: chkWifiHidden
                        text: qsTr("Hidden SSID")

                        onActiveFocusChanged: {
                            if (activeFocus)
                                wifiScroll.scrollToItem(this);
                        }
                    }
                }
            }
        }
    }
    ]
    
    // WPA2/3 PSK validation helpers
    function isAsciiPrintable(text) {
        for (var i = 0; i < text.length; i++) {
            var code = text.charCodeAt(i)
            if (code < 32 || code > 126) {
                return false
            }
        }
        return true
    }

    function isHex64(text) {
        if (text.length !== 64) {
            return false
        }
        for (var i = 0; i < text.length; i++) {
            var code = text.charCodeAt(i)
            var isDigit = code >= 48 && code <= 57  // 0-9
            var isLower = code >= 97 && code <= 102 // a-f
            var isUpper = code >= 65 && code <= 70  // A-F
            if (!(isDigit || isLower || isUpper)) {
                return false
            }
        }
        return true
    }

    function isValidWifiPassword(text) {
        if (!text || text.length === 0) {
            // Allow open networks
            return true
        }
        if (isHex64(text)) {
            return true
        }
        // 8–63 ASCII printable characters
        return text.length >= 8 && text.length <= 63 && isAsciiPrintable(text)
    }

    // Validation: allow proceed when
    // - SSID entered and either new PSK provided or a saved crypt exists; or
    // - all WiFi fields are empty (skip)
    nextButtonEnabled: (function(){
        var haveSSID = fieldWifiSSID.text && fieldWifiSSID.text.trim().length > 0
        if (!haveSSID) return true  // allow skipping by leaving fields empty

        if (wifiMode === "open") return true

        // secure / closed mode
        var ssidNow = fieldWifiSSID.text.trim()
        var canKeep = hadSavedCrypt && ssidUnchanged(ssidNow, originalSavedSSID)
        var pwd = fieldWifiPassword.text || ""

        // If we *can* keep and user left blank, OK
        if (canKeep && pwd.length === 0) {
            return true
        }

        // Need a new password -> must be valid and must match confirm
        if (pwd.length === 0) return false
        if (!root.isValidWifiPassword(pwd)) return false

        if (confirmNeedsInput && (pwd !== (fieldWifiPasswordConfirm.text || ""))) return false
        return true
    })()

    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        var ssid = fieldWifiSSID.text ? fieldWifiSSID.text.trim() : ""
        var pwd = fieldWifiPassword.text
        var prevSSID = saved.wifiSSID || ""
        var hidden = chkWifiHidden.checked
        var hadCryptBefore = !!saved.wifiPasswordCrypt
        var sameSSID = ssidUnchanged(ssid, prevSSID)

        // persist mode
        saved.wifiMode = wifiMode
        
        // Handle SSID and password
        if (ssid.length > 0) {
            saved.wifiSSID = ssid

            if (wifiMode === "open") {
               // always clear in open mode
               delete saved.wifiPasswordCrypt
            } else {
               // secure / closed mode
               if (pwd.length > 0) {
                   // extra safety; normally unreachable because nextButtonEnabled prevents this
                   if (confirmNeedsInput && pwd !== fieldWifiPasswordConfirm.text) return;
                   // overwrite with new password
                   var isPassphrase = (pwd.length >= 8 && pwd.length < 64)
                   saved.wifiPasswordCrypt = isPassphrase ? imageWriter.pbkdf2(pwd, ssid) : pwd
               } else if (hadCryptBefore && sameSSID) {
                   // keep the existing crypt
                   // (do nothing)
               } else {
                   // no password provided and can't keep -> ensure cleared
                   delete saved.wifiPasswordCrypt
               }
            }

            saved.wifiHidden = hidden
            wizardContainer.wifiConfigured = true
        } else {
            // No SSID -> clear SSID and password settings
            delete saved.wifiSSID
            delete saved.wifiPasswordCrypt
            delete saved.wifiHidden
            wizardContainer.wifiConfigured = false
        }
        
        imageWriter.setSavedCustomizationSettings(saved)
        // Do not log sensitive data
    }
    
    // Handle skip button
    onSkipClicked: {
        // Clear all customization flags
        wizardContainer.hostnameConfigured = false
        wizardContainer.localeConfigured = false
        wizardContainer.userConfigured = false
        wizardContainer.wifiConfigured = false
        wizardContainer.sshEnabled = false
        
        // Jump to writing step
        wizardContainer.jumpToStep(wizardContainer.stepWriting)
    }
} 
