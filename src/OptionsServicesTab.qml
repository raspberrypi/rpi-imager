/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.15
import "qmlcomponents"

import RpiImager

OptionsTabBase {
    id: root

    property alias publicKeyModel: publicKeyModel

    property alias chkSSH: chkSSH
    property alias radioPasswordAuthentication: radioPasswordAuthentication
    property alias radioPubKeyAuthentication: radioPubKeyAuthentication

    required property ImageWriter imageWriter
    required property ImCheckBox chkSetUser
    required property TextField fieldUserName
    required property TextField fieldUserPassword

    // Unified SSH key validation function
    function isValidSSHKey(keyText) {
        if (!keyText || keyText.trim().length === 0) return true // Empty is valid
        
        // SSH key regex - supports standard keys, ECDSA curves, security keys, and certificates
        // Standard: ssh-rsa, ssh-ed25519, ssh-dss
        // ECDSA: ecdsa-sha2-nistp256/384/521 (using \d+ for any curve)  
        // Security keys: sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com
        // Certificates: any valid key type + "-cert-v01@openssh.com"
        var sshKeyRegex = /^(ssh-(rsa|ed25519|dss)|ecdsa-sha2-nistp\d+|sk-(ssh-ed25519|ecdsa-sha2-nistp256)@openssh\.com|[\w-]+-cert-v01@openssh\.com) [A-Za-z0-9+\/=]+( .*)?$/
        return sshKeyRegex.test(keyText.trim())
    }

    ColumnLayout {
        Layout.fillWidth: true

        ImCheckBox {
            id: chkSSH
            text: qsTr("Enable SSH")
            onCheckedChanged: {
                if (checked) {
                    if (!radioPasswordAuthentication.checked && !radioPubKeyAuthentication.checked) {
                        radioPasswordAuthentication.checked = true
                    }
                    if (radioPasswordAuthentication.checked) {
                        root.chkSetUser.checked = true
                    }
                    // Re-validate SSH keys when SSH is re-enabled
                    revalidateAllSSHKeys()
                } else {
                    // Clear SSH key errors when SSH is disabled
                    clearAllSSHKeyErrors()
                }
            }
        }

        ImRadioButton {
            id: radioPasswordAuthentication
            enabled: chkSSH.checked
            text: qsTr("Use password authentication")
            onCheckedChanged: {
                if (checked) {
                    root.chkSetUser.checked = true
                    //root.fieldUserPassword.forceActiveFocus()
                    // Clear SSH key errors when switching to password authentication
                    clearAllSSHKeyErrors()
                }
            }
        }
        ImRadioButton {
            id: radioPubKeyAuthentication
            enabled: chkSSH.checked
            text: qsTr("Allow public-key authentication only")
            onCheckedChanged: {
                if (checked) {
                    if (root.chkSetUser.checked && root.fieldUserName.text == "pi" && root.fieldUserPassword.text.length == 0) {
                        root.chkSetUser.checked = false
                    }
                    // Re-validate SSH keys when switching to public key authentication
                    // Use a timer to ensure the enabled state has settled before re-validating
                    revalidationTimer.restart()
                }
            }
        }

        Text {
            text: qsTr("Set authorized_keys for '%1':").arg(root.fieldUserName.text)
            color: radioPubKeyAuthentication.checked ? Style.formLabelColor : Style.formLabelDisabledColor
            // textFormat: Text.PlainText
            Layout.leftMargin: 40
        }
        Item {
            id: publicKeyListViewContainer
            Layout.leftMargin: 40
            Layout.rightMargin: 5
            Layout.minimumHeight: 50
            Layout.fillHeight: true
            Layout.preferredWidth: root.width - (20 + Layout.leftMargin + Layout.rightMargin)

            ListView {
                id: publicKeyList
                model: ListModel {
                    id: publicKeyModel

                }
                boundsBehavior: Flickable.StopAtBounds
                anchors.fill: parent
                spacing: 12
                clip: true

                delegate: Column {                       
                    id: publicKeyItem
                    required property int index
                    required property string publicKeyField

                    width: publicKeyList.width
                    spacing: 4

                    RowLayout {
                        width: parent.width
                        
                        TextField {
                            id: contentField
                            enabled: radioPubKeyAuthentication.checked
                            text: publicKeyItem.publicKeyField
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            
                            property bool indicateError: false
                            
                            // Handle error state when field is disabled/enabled
                            onEnabledChanged: {
                                if (!enabled) {
                                    indicateError = false
                                } else {
                                    // Re-validate when field becomes enabled
                                    if (text.length > 0 && !root.isValidSSHKey(text)) {
                                        indicateError = true
                                    } else {
                                        indicateError = false
                                    }
                                }
                            }
                            

                            
                            // Visual feedback for validation errors
                            color: indicateError ? Style.formLabelErrorColor : (enabled ? "black" : "grey")
                            
                            // Helpful tooltip on hover
                            ToolTip.visible: hovered && text.length === 0
                            ToolTip.text: qsTr("Paste your SSH public key here.\nSupported formats: ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, and SSH certificates\nExample: ssh-rsa AAAAB3NzaC1yc2E... user@hostname")
                            ToolTip.delay: 1000
                            
                            onTextChanged: {
                                // Always validate on text change and update error state accordingly
                                if (text.length === 0 || root.isValidSSHKey(text)) {
                                    indicateError = false
                                } else {
                                    indicateError = true
                                }
                                
                                // Update the model with current text so hasSSHKeyValidationErrors() sees current state
                                if (enabled) {
                                    publicKeyModel.set(publicKeyItem.index, {publicKeyField: text})
                                }
                            }

                            onEditingFinished: {
                                // Final validation check when editing is finished
                                if (text.length > 0 && !root.isValidSSHKey(text)) {
                                    indicateError = true
                                } else {
                                    indicateError = false
                                }
                                // Always update the model with the current text
                                publicKeyModel.set(publicKeyItem.index, {publicKeyField: contentField.text})
                            }
                        }
                        
                        ImButton {
                            id: removePublicKeyItem
                            text: qsTr("Delete Key")
                            enabled: root.radioPubKeyAuthentication.checked
                            Layout.minimumWidth: 100
                            Layout.preferredWidth: 100
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                            onClicked: {
                                // Find the correct index by matching the actual key data
                                // This is more reliable than using delegate index which can become stale
                                var keyToDelete = publicKeyItem.publicKeyField
                                for (var i = 0; i < publicKeyModel.count; i++) {
                                    var modelItem = publicKeyModel.get(i)
                                    if (modelItem && modelItem.publicKeyField === keyToDelete) {
                                        publicKeyModel.remove(i)
                                        publicKeyListViewContainer.implicitHeight -= 50 + publicKeyList.spacing
                                        break
                                    }
                                }
                            }
                        }
                    }
                    
                    Text {
                        id: errorText
                        visible: contentField.indicateError
                        text: qsTr("Invalid SSH key format. SSH keys must start with ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, or SSH certificates, followed by the key data and optional comment.")
                        color: Style.formLabelErrorColor
                        font.pointSize: 9
                        wrapMode: Text.WordWrap
                        width: parent.width - 20
                        leftPadding: 10
                    }
                }
            }
        }

        RowLayout {
            ImButton {
                text: qsTr("RUN SSH-KEYGEN")
                Layout.leftMargin: 40
                enabled: root.imageWriter.hasSshKeyGen() && !root.imageWriter.hasPubKey()
                onClicked: {
                    enabled = false
                    root.imageWriter.generatePubKey()
                    publicKeyModel.append({publicKeyField: root.imageWriter.getDefaultPubKey()})
                }
            }
            ImButton {
                text: qsTr("Add SSH Key")
                Layout.leftMargin: 40
                enabled: radioPubKeyAuthentication.checked
                onClicked: {
                    publicKeyModel.append({publicKeyField: ""})
                    if (publicKeyListViewContainer.implicitHeight) {
                        publicKeyListViewContainer.implicitHeight += 50 + publicKeyList.spacing
                    } else {
                        publicKeyListViewContainer.implicitHeight += 100 + (publicKeyList.spacing)
                    }
                }
            }
        }
    }
    
    // Timer to handle delayed re-validation when switching authentication modes
    Timer {
        id: revalidationTimer
        interval: 50 // Small delay to let UI settle
        onTriggered: revalidateAllSSHKeys()
    }
    
    function clearAllSSHKeyErrors() {
        // Clear validation errors from all SSH key fields
        // Note: This is a workaround since we can't easily access delegate items directly
        // The actual clearing will happen when the fields become disabled or when validation
        // logic runs again, but this ensures consistent state
        for (var i = 0; i < publicKeyModel.count; i++) {
            var item = publicKeyModel.get(i)
            if (item && item.publicKeyField) {
                // Trigger a data change to potentially refresh validation state
                publicKeyModel.set(i, {publicKeyField: item.publicKeyField})
            }
        }
    }
    
    function revalidateAllSSHKeys() {
        // Re-validate all SSH keys when section is re-enabled
        // The onEnabledChanged handlers should handle the visual state updates
        // We just need to ensure the model is up to date
        for (var i = 0; i < publicKeyModel.count; i++) {
            var item = publicKeyModel.get(i)
            if (item && item.publicKeyField) {
                publicKeyModel.set(i, {publicKeyField: item.publicKeyField})
            }
        }
    }
    
    function hasSSHKeyValidationErrors() {
        // Directly validate all SSH keys in the model - this is the most reliable approach
        // since it doesn't depend on maintaining counter state
        for (var i = 0; i < publicKeyModel.count; i++) {
            var keyData = publicKeyModel.get(i)
            if (keyData && keyData.publicKeyField && keyData.publicKeyField.length > 0) {
                // Validate the SSH key using the unified validation function
                if (!isValidSSHKey(keyData.publicKeyField)) {
                    return true
                }
            }
        }
        return false
    }
    
    function forceListViewRefresh() {
        // Force the ListView to completely refresh its delegates
        // This is needed when the model is cleared and reloaded to ensure proper rendering
        
        // Reset the container height based on actual model count
        var newHeight = publicKeyModel.count * (50 + publicKeyList.spacing)
        if (newHeight > 0) {
            publicKeyListViewContainer.implicitHeight = newHeight
        } else {
            publicKeyListViewContainer.implicitHeight = 50 // Minimum height
        }
        
        // Force the ListView to refresh by temporarily setting model to null and back
        var tempModel = publicKeyList.model
        publicKeyList.model = null
        publicKeyList.model = tempModel
    }
}
