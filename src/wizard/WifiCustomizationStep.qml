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

    function ssidUnchanged(ssid, prev) { return (ssid || "") === (prev || "") }
    
    title: qsTr("Customisation: Choose Wi‑Fi")
    subtitle: qsTr("Configure wireless LAN settings")
    showSkipButton: true
    
    // Set initial focus on SSID
    // don't default to the mode switchers so they aren't tempted to select open ;)
    initialFocusItem: fieldWifiSSID

    Component.onCompleted: {
        root.registerFocusGroup("wifi_modes", function() {
            return [tabSecure, tabOpen]
        }, 0)
        root.registerFocusGroup("wifi_fields", function(){
            return [fieldWifiSSID, fieldWifiPassword, fieldWifiCountry]
        }, 1)
        root.registerFocusGroup("wifi_options", function(){ return [chkWifiHidden] }, 2)

        // Prefill from saved settings
        var saved = imageWriter.getSavedCustomizationSettings()

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

        // Note: WiFi country is initialized by fieldWifiCountry's Component.onCompleted
        // after the model is loaded, which properly handles recommended/saved/default values
        if (saved.wifiHidden !== undefined) {
            chkWifiHidden.checked = (saved.wifiHidden === true || saved.wifiHidden === "true")
        }

        originalSavedSSID = saved.wifiSSID || ""
        // Remember if a crypted PSK is already saved (affects placeholder/keep semantics)
        hadSavedCrypt = !!saved.wifiPasswordCrypt

        // if no saved crypt, try to prefill a PSK from system
        if (!hadSavedCrypt) {
            // Auto-populate WiFi password from system keychain when available
            // Only when no crypted password is already saved
            var psk = imageWriter.getPSK()
            if (psk && psk.length > 0) fieldWifiPassword.text = psk
        }

        // wifiMode: prefer saved value; otherwise infer from whether a password is present
        wifiMode = (saved.wifiMode === "secure" || saved.wifiMode === "open")
            ? saved.wifiMode
            : "secure"

        updatePasswordFieldUI()
    }

    function updatePasswordFieldUI() {
        var ssid = (fieldWifiSSID.text || "").trim()
        var prevSSID = originalSavedSSID
        if (wifiMode === "open") {
            fieldWifiPassword.enabled = false
            fieldWifiPassword.text = ""
            fieldWifiPassword.placeholderText = qsTr("No password (open network)")
        } else {
            fieldWifiPassword.enabled = true
            var canKeep = hadSavedCrypt && ssidUnchanged(ssid, prevSSID)
            fieldWifiPassword.placeholderText = canKeep
                ? qsTr("Saved (hidden) — leave blank to keep")
                : qsTr("Network password")
        }
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
                        active: wifiMode === "secure"
                        onClicked: { wifiMode = "secure"; updatePasswordFieldUI() }

                        onActiveFocusChanged: {
                            if (activeFocus) wifiScroll.scrollToItem(this);
                        }
                    }

                    ImToggleTab {
                        id: tabOpen
                        text: qsTr("Open network")
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
                        placeholderText: qsTr("Network name")
                        font.pixelSize: Style.fontSizeInput
                        onTextChanged: updatePasswordFieldUI()
                    }

                    WizardFormLabel {
                        id: lblPassword
                        text: CommonStrings.password
                        visible: wifiMode === "secure"
                    }

                    ImTextField {
                        id: fieldWifiPassword
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        font.pixelSize: Style.fontSizeInput
                        visible: wifiMode === "secure"
                    }

                    WizardFormLabel {
                        text: qsTr("Wireless LAN country:")
                    }

                    ImComboBox {
                        id: fieldWifiCountry
                        Layout.fillWidth: true
                        model: []
                        font.pixelSize: Style.fontSizeInput
                        property bool isInitializing: true
                        Component.onCompleted: {
                            model = root.imageWriter.getCountryList()
                            // Always use recommended country from capital city selection
                            // Priority: recommendation from capital city > default (GB)
                            // We intentionally ignore any previously saved wifiCountry
                            var saved = root.imageWriter.getSavedCustomizationSettings()
                            console.log("WifiCustomizationStep: recommendedWifiCountry =", saved.recommendedWifiCountry)
                            if (saved && saved.recommendedWifiCountry && model && model.length > 0) {
                                // Use recommended country from capital city selection
                                var target = saved.recommendedWifiCountry
                                var idx = -1
                                for (var i = 0; i < model.length; i++) {
                                    if (model[i] === target) { idx = i; break }
                                }
                                if (idx >= 0) {
                                    currentIndex = idx
                                } else {
                                    fieldWifiCountry.editText = target
                                }
                            } else if (model && model.length > 0) {
                                // Default to GB if available, else first item
                                var gbIndex = -1
                                for (var j = 0; j < model.length; j++) {
                                    if (model[j] === "GB") { gbIndex = j; break }
                                }
                                currentIndex = (gbIndex >= 0) ? gbIndex : 0
                            }
                            isInitializing = false
                            // Now that initialization is complete, update the label once
                            recommendedLabel.updateVisibility()
                        }
                        onCurrentTextChanged: {
                            // Update visibility when selection changes (but not during initialization)
                            if (!isInitializing) {
                                recommendedLabel.updateVisibility()
                            }
                        }
                    }

                    // Empty label to maintain grid alignment
                    Item { width: 1; height: 1 }

                    Text {
                        id: recommendedLabel
                        Layout.fillWidth: true
                        Layout.columnSpan: 1
                        color: "#4CAF50"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap

                        function updateVisibility() {
                            var saved = root.imageWriter.getSavedCustomizationSettings()
                            if (saved && saved.recommendedWifiCountry) {
                                var currentCountry = fieldWifiCountry.currentText || fieldWifiCountry.editText
                                visible = (currentCountry === saved.recommendedWifiCountry)
                                text = visible ? qsTr("✓ Recommended based on your capital city selection") : ""
                                console.log("WifiCustomizationStep: recommendation label visibility =", visible, "current =", currentCountry, "recommended =", saved.recommendedWifiCountry)
                            } else {
                                visible = false
                                text = ""
                                console.log("WifiCustomizationStep: no recommendation available")
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.spacingMedium

                    ImCheckBox {
                        id: chkWifiHidden
                        text: qsTr("Hidden SSID")

                        onActiveFocusChanged: {
                            if (activeFocus)
                                wifiScroll.scrollToItem(this);
                        }
                    }

                    Item {
                        Layout.fillWidth: true
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
    // - SSID and country entered and either new PSK provided or a saved crypt exists; or
    // - all WiFi fields are empty (skip)
    nextButtonEnabled: (function(){
        var haveSSID = fieldWifiSSID.text && fieldWifiSSID.text.trim().length > 0
        var haveCountry = (fieldWifiCountry.currentText || fieldWifiCountry.editText)
        if (!haveSSID || !haveCountry) return true  // allow skipping by leaving fields empty as before

        if (wifiMode === "open") return true

        // secure / closed mode
        var canKeep = hadSavedCrypt && ssidUnchanged(fieldWifiSSID.text.trim(), originalSavedSSID)
        if ((fieldWifiPassword.text || "").length === 0) {
            return canKeep // empty only ok if we can keep the old crypt
        }
        return root.isValidWifiPassword(fieldWifiPassword.text)
    })()

    // Save settings when moving to next step
    onNextClicked: {
        // Merge-and-save strategy
        var saved = imageWriter.getSavedCustomizationSettings()
        var ssid = fieldWifiSSID.text ? fieldWifiSSID.text.trim() : ""
        var country = fieldWifiCountry.currentText || fieldWifiCountry.editText || ""
        var pwd = fieldWifiPassword.text
        var prevSSID = saved.wifiSSID || ""
        var hidden = chkWifiHidden.checked
        var hadCryptBefore = !!saved.wifiPasswordCrypt
        var sameSSID = ssidUnchanged(ssid, prevSSID)

        // persist mode
        saved.wifiMode = wifiMode
        
        // Save country code if provided (even without SSID)
        if (country.length > 0) {
            saved.wifiCountry = country
            wizardContainer.wifiConfigured = true
        } else {
            // No country -> clear country setting
            delete saved.wifiCountry
        }
        
        // Handle SSID and password
        if (ssid.length > 0) {
            saved.wifiSSID = ssid

            if (wifiMode === "open") {
               // always clear in open mode
               delete saved.wifiPasswordCrypt
            } else {
               // secure / closed mode
               if (pwd.length > 0) {
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
            // If we have a country, still mark as configured
            if (country.length === 0) {
                wizardContainer.wifiConfigured = false
            }
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
