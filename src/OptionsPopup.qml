/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15
import QtQuick.Controls.Material 2.2
import QtQuick.Window 2.15
import "qmlcomponents"

import RpiImager

Window {
    id: popup
    width: 550
    height: 420

    minimumWidth: width
    // Deliberately do not set a maximum width - if the user wants to resize, let them.
    //maximumWidth: width

    minimumHeight: height
    // Deliberately do not set a maximum height - if the user wants to resize, let them.
    //maximumHeight: height

    title: qsTr("OS Customization")

    modality: Qt.WindowModal

    required property ImageWriter imageWriter

    property bool initialized: false
    property bool hasSavedSettings: false
    property bool changesAppliedInSession: false
    property string config
    property string cmdline
    property string firstrun
    property string cloudinit
    property string cloudinitrun
    property string cloudinitwrite
    property string cloudinitnetwork

    signal saveSettingsSignal(var settings)

    Shortcut {
        sequence: "Esc"
        onActivated: popup.close()
    }

    // Keyboard navigation shortcuts
    function focusFirstElementInCurrentTab() {
        // Focus the first focusable element in the current tab
        Qt.callLater(function() {
            if (bar.currentIndex === 0) {
                // General tab - focus hostname checkbox
                generalTab.chkHostname.forceActiveFocus()
            } else if (bar.currentIndex === 1) {
                // Services tab - focus SSH checkbox
                remoteAccessTab.chkSSH.forceActiveFocus()
            } else if (bar.currentIndex === 2) {
                // Options tab - focus beep checkbox
                optionsTab.chkBeep.forceActiveFocus()
            }
        })
    }
    
    function focusLastElementInCurrentTab() {
        // Focus the last focusable element in the current tab
        Qt.callLater(function() {
            if (bar.currentIndex === 0) {
                // General tab - focus keyboard layout combo
                generalTab.fieldKeyboardLayout.forceActiveFocus()
            } else if (bar.currentIndex === 1) {
                // Services tab - need to find a reliable way to access Add SSH Key button
                // For now, just focus the second radio button as it's always accessible
                remoteAccessTab.radioPubKeyAuthentication.forceActiveFocus()
            } else if (bar.currentIndex === 2) {
                // Options tab - focus telemetry checkbox
                optionsTab.chkTelemtry.forceActiveFocus()
            }
        })
    }
    
    Shortcut {
        sequence: "Ctrl+Tab"
        onActivated: {
            bar.currentIndex = (bar.currentIndex + 1) % 3
            focusFirstElementInCurrentTab()
        }
    }
    
    Shortcut {
        sequence: "Ctrl+Shift+Tab"
        onActivated: {
            bar.currentIndex = (bar.currentIndex + 2) % 3  // +2 for going backwards in modulo 3
            focusFirstElementInCurrentTab()
        }
    }

    // Also support Ctrl+PageDown/PageUp as alternative tab switching
    Shortcut {
        sequence: "Ctrl+PgDown"
        onActivated: {
            bar.currentIndex = (bar.currentIndex + 1) % 3
            focusFirstElementInCurrentTab()
        }
    }
    
    Shortcut {
        sequence: "Ctrl+PgUp"
        onActivated: {
            bar.currentIndex = (bar.currentIndex + 2) % 3
            focusFirstElementInCurrentTab()
        }
    }

    onClosing: {
        // If user closes without saving, don't apply unsaved changes
        if (!changesAppliedInSession) {
            restorePreviousState()
        }
    }

    ColumnLayout {
        anchors {
            top: parent.top
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        spacing: 0

        TabBar {
            id: bar
            Layout.fillWidth: true
            focus: true
            activeFocusOnTab: true
            
            // Allow arrow key navigation between tabs
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Up) {
                    currentIndex = (currentIndex + 2) % 3  // Go backwards
                    event.accepted = true
                } else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down) {
                    currentIndex = (currentIndex + 1) % 3  // Go forwards  
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab) {
                    // Allow Tab to move to first element in current tab
                    focusFirstElementInCurrentTab()
                    event.accepted = true
                }
            }

            TabButton {
                text: qsTr("General") + (hasGeneralTabErrors() ? " ⚠" : "")
                onClicked: {
                    generalTab.scrollPosition = 0
                    
                    // Auto-focus first error field if there are validation errors
                    if (hasGeneralTabErrors()) {
                        focusFirstGeneralTabError()
                    } else {
                        // Always transfer focus to tab content when clicked
                        focusFirstElementInCurrentTab()
                    }
                }
            }
            TabButton {
                text: qsTr("Services") + (hasServicesTabErrors() ? " ⚠" : "")
                onClicked: {
                    remoteAccessTab.scrollPosition = 0
                    
                    // Auto-focus first error field if there are validation errors
                    if (hasServicesTabErrors()) {
                        focusFirstServicesTabError()
                    } else {
                        // Always transfer focus to tab content when clicked
                        focusFirstElementInCurrentTab()
                    }
                }
            }
            TabButton {
                text: qsTr("Options")
                onClicked: {
                    optionsTab.scrollPosition = 0
                    
                    // Always transfer focus to tab content when clicked
                    focusFirstElementInCurrentTab()
                }
            }
        }

        StackLayout {
            id: optionsStack
            Layout.rightMargin: 10
            Layout.leftMargin: 10
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: bar.currentIndex

            OptionsGeneralTab {
                id: generalTab
                sshEnabled: remoteAccessTab.chkSSH.enabled
                passwordAuthenticationEnabled: remoteAccessTab.radioPasswordAuthentication.enabled
                navigateToButtons: function() { cancelButton.forceActiveFocus() }
                tabBar: bar
            }

            OptionsServicesTab {
                id: remoteAccessTab
                imageWriter: popup.imageWriter
                chkSetUser: generalTab.chkSetUser
                fieldUserName: generalTab.fieldUserName
                fieldUserPassword: generalTab.fieldUserPassword
                navigateToButtons: function() { cancelButton.forceActiveFocus() }
                tabBar: bar
            }

            OptionsMiscTab {
                id: optionsTab
                navigateToButtons: function() { cancelButton.forceActiveFocus() }
                tabBar: bar
            }
        }

        RowLayout {
            id: buttonsRow
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }

            ImButton {
                id: cancelButton
                text: qsTr("CANCEL")
                activeFocusOnTab: true
                
                // Add keyboard navigation
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        // Tab to Save button
                        saveButton.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier)) {
                        // Shift+Tab back to last element in current tab
                        focusLastElementInCurrentTab()
                        event.accepted = true
                    }
                }
                
                onClicked: {
                    popup.close()
                }
            }

            ImButtonRed {
                id: saveButton
                text: qsTr("SAVE")
                enabled: !hasValidationErrors()
                activeFocusOnTab: true
                
                // Show helpful tooltip when button is disabled
                ToolTip.visible: hovered && !enabled
                ToolTip.text: {
                    if (hasGeneralTabErrors() && hasServicesTabErrors()) {
                        return qsTr("Please fix validation errors in General and Services tabs")
                    } else if (hasGeneralTabErrors()) {
                        return qsTr("Please fix validation errors in General tab")
                    } else if (hasServicesTabErrors()) {
                        return qsTr("Please fix validation errors in Services tab")
                    } else {
                        return ""
                    }
                }
                ToolTip.delay: 500
                
                // Add keyboard navigation  
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        // Tab wraps back to TabBar
                        bar.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier)) {
                        // Shift+Tab to Cancel button
                        cancelButton.forceActiveFocus()
                        event.accepted = true
                    }
                }
                
                onClicked: {
                    // Safety check - don't save if there are validation errors
                    // This should rarely trigger since the button is disabled when there are errors,
                    // but provides a fallback and focuses the first error field for better UX
                    if (hasValidationErrors()) {
                        focusFirstErrorField()
                        return
                    }
                    
                    popup.applySettings()
                    popup.saveSettings()
                    changesAppliedInSession = true
                    popup.close()
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    function hasGeneralTabErrors() {
        // Check hostname validation (only if hostname is enabled)
        if (generalTab.chkHostname.checked && generalTab.fieldHostname.indicateError) {
            return true
        }
        
        // Check user account validation (only if user account is enabled)
        if (generalTab.chkSetUser.checked && (generalTab.fieldUserName.indicateError || generalTab.fieldUserPassword.indicateError)) {
            return true
        }
        
        // Check Wi-Fi validation (only if Wi-Fi is enabled)
        if (generalTab.chkWifi.checked && (generalTab.fieldWifiSSID.indicateError || generalTab.fieldWifiPassword.indicateError)) {
            return true
        }
        
        return false
    }
    
    function hasServicesTabErrors() {
        // Check SSH key validation (only if SSH is enabled and public key auth is selected)
        if (remoteAccessTab.chkSSH.checked && remoteAccessTab.radioPubKeyAuthentication.checked) {
            // Use the dedicated function that can access delegate validation state
            if (remoteAccessTab.hasSSHKeyValidationErrors()) {
                return true
            }
        }
        
        return false
    }
    
    function hasValidationErrors() {
        return hasGeneralTabErrors() || hasServicesTabErrors()
    }

    function focusFirstGeneralTabError() {
        // Focus the first error field in the General tab
        if (generalTab.chkHostname.checked && generalTab.fieldHostname.indicateError) {
            generalTab.fieldHostname.forceActiveFocus()
            return
        }
        
        if (generalTab.chkSetUser.checked && generalTab.fieldUserName.indicateError) {
            generalTab.fieldUserName.forceActiveFocus()
            return
        }
        
        if (generalTab.chkSetUser.checked && generalTab.fieldUserPassword.indicateError) {
            generalTab.fieldUserPassword.forceActiveFocus()
            return
        }
        
        if (generalTab.chkWifi.checked && generalTab.fieldWifiSSID.indicateError) {
            generalTab.fieldWifiSSID.forceActiveFocus()
            return
        }
        
        if (generalTab.chkWifi.checked && generalTab.fieldWifiPassword.indicateError) {
            generalTab.fieldWifiPassword.forceActiveFocus()
            return
        }
    }
    
    function focusFirstServicesTabError() {
        // Focus the first error field in the Services tab
        if (remoteAccessTab.chkSSH.checked && remoteAccessTab.radioPubKeyAuthentication.checked) {
            // Try to focus the first invalid SSH key field
            remoteAccessTab.focusFirstInvalidSSHKey()
        }
    }

    function focusFirstErrorField() {
        // Focus the first field with a validation error for better UX
        // Check in order of tabs: General -> Services
        
        // General tab errors
        if (hasGeneralTabErrors()) {
            bar.currentIndex = 0
            focusFirstGeneralTabError()
            return
        }
        
                 // Services tab errors (SSH keys)
         if (hasServicesTabErrors()) {
             bar.currentIndex = 1
             focusFirstServicesTabError()
             return
         }
    }

    function initialize() {
        optionsTab.beepEnabled = imageWriter.getBoolSetting("beep")
        optionsTab.telemetryEnabled = imageWriter.getBoolSetting("telemetry")
        optionsTab.ejectEnabled = imageWriter.getBoolSetting("eject")
        var settings = imageWriter.getSavedCustomizationSettings()
        generalTab.fieldTimezone.model = imageWriter.getTimezoneList()
        generalTab.fieldWifiCountry.model = imageWriter.getCountryList()
        generalTab.fieldKeyboardLayout.model = imageWriter.getKeymapLayoutList()

        if (Object.keys(settings).length) {
            hasSavedSettings = true
        }
        if ('hostname' in settings) {
            generalTab.fieldHostname.text = settings.hostname
            generalTab.chkHostname.checked = true
        }
        if ('sshUserPassword' in settings) {
            generalTab.fieldUserPassword.text = settings.sshUserPassword
            generalTab.fieldUserPassword.alreadyCrypted = true
            generalTab.chkSetUser.checked = true
            /* Older imager versions did not have a sshEnabled setting.
               Assume it is true if it does not exists and sshUserPassword is set */
            if (!('sshEnabled' in settings) || settings.sshEnabled === "true" || settings.sshEnabled === true) {
                remoteAccessTab.chkSSH.checked = true
                remoteAccessTab.radioPasswordAuthentication.checked = true
            }
        }
        if ('sshAuthorizedKeys' in settings) {
            var possiblePublicKeys = settings.sshAuthorizedKeys.split('\n')

            for (const publicKey of possiblePublicKeys) {
                var pkitem = publicKey.trim()
                if (pkitem) {
                    remoteAccessTab.publicKeyModel.append({publicKeyField: pkitem})
                }
            }

            remoteAccessTab.radioPubKeyAuthentication.checked = true
            remoteAccessTab.chkSSH.checked = true
        }
        // Attempt to insert the default public key, but avoid clashes.
        {
            var insertDefaultKey = true
            var defaultKey = imageWriter.getDefaultPubKey()
            for (var i = 0; i<remoteAccessTab.publicKeyModel.count; i++) {
                if (remoteAccessTab.publicKeyModel.get(i)["publicKeyField"] == defaultKey) {
                    insertDefaultKey = false
                    break;
                }
            }
            if (insertDefaultKey) {
                remoteAccessTab.publicKeyModel.append({publicKeyField: defaultKey})
            }
        }

        if ('sshUserName' in settings) {
            generalTab.fieldUserName.text = settings.sshUserName
            generalTab.chkSetUser.checked = true
        } else {
            generalTab.fieldUserName.text = imageWriter.getCurrentUser()
        }

        if ('wifiSSID' in settings) {
            generalTab.fieldWifiSSID.text = settings.wifiSSID
            if ('wifiSSIDHidden' in settings && settings.wifiSSIDHidden) {
                generalTab.chkWifiSSIDHidden.checked = true
            }
            generalTab.fieldWifiPassword.text = settings.wifiPassword
            generalTab.fieldWifiCountry.currentIndex = generalTab.fieldWifiCountry.find(settings.wifiCountry)
            if (generalTab.fieldWifiCountry.currentIndex == -1) {
                generalTab.fieldWifiCountry.editText = settings.wifiCountry
            }
            generalTab.chkWifi.checked = true
        } else {
            generalTab.fieldWifiCountry.currentIndex = generalTab.fieldWifiCountry.find("GB")
            generalTab.fieldWifiSSID.text = imageWriter.getSSID()
            if (generalTab.fieldWifiSSID.text.length) {
                generalTab.fieldWifiPassword.text = imageWriter.getPSK()
                if (generalTab.fieldWifiPassword.text.length) {
                    if (Qt.platform.os == "osx") {
                        /* User indicated wifi must be prefilled */
                        generalTab.chkWifi.checked = true
                    }
                }
            }
        }

        var tz;
        if ('timezone' in settings) {
            generalTab.chkLocale.checked = true
            tz = settings.timezone
        } else {
            tz = imageWriter.getTimezone()
        }
        var tzidx = generalTab.fieldTimezone.find(tz)
        if (tzidx === -1) {
            generalTab.fieldTimezone.editText = tz
        } else {
            generalTab.fieldTimezone.currentIndex = tzidx
        }
        if ('keyboardLayout' in settings) {
            generalTab.fieldKeyboardLayout.currentIndex = generalTab.fieldKeyboardLayout.find(settings.keyboardLayout)
            if (generalTab.fieldKeyboardLayout.currentIndex == -1) {
                generalTab.fieldKeyboardLayout.editText = settings.keyboardLayout
            }
        } else {
            if (imageWriter.isEmbeddedMode())
            {
                generalTab.fieldKeyboardLayout.currentIndex = generalTab.fieldKeyboardLayout.find(imageWriter.getCurrentKeyboard())
            }
            else
            {
                /* Lacking an easy cross-platform to fetch keyboard layout
                   from host system, just default to "gb" for people in
                   UK time zone for now, and "us" for everyone else */
                if (tz === "Europe/London") {
                    generalTab.fieldKeyboardLayout.currentIndex = generalTab.fieldKeyboardLayout.find("gb")
                } else {
                    generalTab.fieldKeyboardLayout.currentIndex = generalTab.fieldKeyboardLayout.find("us")
                }
            }
        }

        if (imageWriter.isEmbeddedMode()) {
            /* For some reason there is no password mask character set by default on Embedded edition */
            var bulletCharacter = String.fromCharCode(0x2022);
            generalTab.fieldUserPassword.passwordCharacter = bulletCharacter;
            generalTab.fieldWifiPassword.passwordCharacter = bulletCharacter;
        }

        initialized = true
    }

    function openPopup() {
        if (!initialized) {
            initialize()
            if (imageWriter.hasSavedCustomizationSettings())
            {
                applySettings()
            }
        }
        
        // Reset the session flag when opening the popup
        changesAppliedInSession = false

        //open()
        show()
        raise()
        
        // Set initial focus to TabBar for keyboard navigation
        bar.forceActiveFocus()
    }

    function addCmdline(s) {
        cmdline += " "+s
    }
    function addConfig(s) {
        config += s+"\n"
    }
    function addFirstRun(s) {
        firstrun += s+"\n"
    }
    function escapeshellarg(arg) {
        return "'"+arg.replace(/'/g, "'\"'\"'")+"'"
    }
    function addCloudInit(s) {
        cloudinit += s+"\n"
    }
    function addCloudInitWriteFile(name, content, perms) {
        cloudinitwrite += "- encoding: b64\n"
        cloudinitwrite += "  content: "+Qt.btoa(content)+"\n"
        cloudinitwrite += "  owner: root:root\n"
        cloudinitwrite += "  path: "+name+"\n"
        cloudinitwrite += "  permissions: '"+perms+"'\n"
    }
    function addCloudInitRun(cmd) {
        cloudinitrun += "- "+cmd+"\n"
    }

    function applySettings()
    {
        cmdline = ""
        config = ""
        firstrun = ""
        cloudinit = ""
        cloudinitrun = ""
        cloudinitwrite = ""
        cloudinitnetwork = ""

        // Pre-trim ComboBox values to avoid repeated trim() calls
        var wifiCountry = generalTab.fieldWifiCountry.editText.trim()
        var timezone = generalTab.fieldTimezone.editText.trim()
        var keyboardLayout = generalTab.fieldKeyboardLayout.editText.trim()

        if (generalTab.chkHostname.checked && generalTab.fieldHostname.length) {
            addFirstRun("CURRENT_HOSTNAME=`cat /etc/hostname | tr -d \" \\t\\n\\r\"`")
            addFirstRun("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then")
            addFirstRun("   /usr/lib/raspberrypi-sys-mods/imager_custom set_hostname "+generalTab.fieldHostname.text)
            addFirstRun("else")
            addFirstRun("   echo "+generalTab.fieldHostname.text+" >/etc/hostname")
            addFirstRun("   sed -i \"s/127.0.1.1.*$CURRENT_HOSTNAME/127.0.1.1\\t"+generalTab.fieldHostname.text+"/g\" /etc/hosts")
            addFirstRun("fi")

            addCloudInit("hostname: "+generalTab.fieldHostname.text)
            addCloudInit("manage_etc_hosts: true")
            addCloudInit("packages:")
            addCloudInit("- avahi-daemon")
            /* Disable date/time checks in apt as NTP may not have synchronized yet when installing packages */
            addCloudInit("apt:")
            addCloudInit("  conf: |")
            addCloudInit("    Acquire {")
            addCloudInit("      Check-Date \"false\";")
            addCloudInit("    };")
            addCloudInit("")
        }

        if (remoteAccessTab.chkSSH.checked || generalTab.chkSetUser.checked) {
            // First user may not be called 'pi' on all distributions, so look username up
            addFirstRun("FIRSTUSER=`getent passwd 1000 | cut -d: -f1`");
            addFirstRun("FIRSTUSERHOME=`getent passwd 1000 | cut -d: -f6`")

            addCloudInit("users:")
            addCloudInit("- name: "+generalTab.fieldUserName.text)
            addCloudInit("  groups: users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo")
            addCloudInit("  shell: /bin/bash")

            var cryptedPassword;
            if (generalTab.chkSetUser.checked) {
                cryptedPassword = generalTab.fieldUserPassword.alreadyCrypted ? generalTab.fieldUserPassword.text : imageWriter.crypt(generalTab.fieldUserPassword.text)
                addCloudInit("  lock_passwd: false")
                addCloudInit("  passwd: "+cryptedPassword)
            }

            if (remoteAccessTab.chkSSH.checked && remoteAccessTab.radioPubKeyAuthentication.checked) {
                var pubkeySpaceSep = ''
                var pubKeyNewlineSep = ''

                for (var j=0; j<remoteAccessTab.publicKeyModel.count; j++) {
                    var pkitem = remoteAccessTab.publicKeyModel.get(j)["publicKeyField"];
                    if (pkitem) {
                        pubkeySpaceSep += ' '+escapeshellarg(pkitem)
                        pubKeyNewlineSep += escapeshellarg(pkitem) + '\n'
                    }
                }

                addFirstRun("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then")
                addFirstRun("   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh -k"+pubkeySpaceSep)
                addFirstRun("else")
                addFirstRun("   install -o \"$FIRSTUSER\" -m 700 -d \"$FIRSTUSERHOME/.ssh\"")
                addFirstRun("   install -o \"$FIRSTUSER\" -m 600 <(printf \""+pubKeyNewlineSep.replace(/\n/g, "\\n")+"\") \"$FIRSTUSERHOME/.ssh/authorized_keys\"")
                addFirstRun("   echo 'PasswordAuthentication no' >>/etc/ssh/sshd_config")
                addFirstRun("   systemctl enable ssh")
                addFirstRun("fi")

                if (!generalTab.chkSetUser.checked) {
                    addCloudInit("  lock_passwd: true")
                }
                addCloudInit("  ssh_authorized_keys:")
                for (var i=0; i<remoteAccessTab.publicKeyModel.count; i++) {
                    var pk = remoteAccessTab.publicKeyModel.get(i)["publicKeyField"].trim();
                    if (pk) {
                        addCloudInit("    - "+pk)
                    }
                }
                addCloudInit("  sudo: ALL=(ALL) NOPASSWD:ALL")
            }
            addCloudInit("")

            if (remoteAccessTab.chkSSH.checked && remoteAccessTab.radioPasswordAuthentication.checked) {
                addCloudInit("ssh_pwauth: true")
                addFirstRun("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then")
                addFirstRun("   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh")
                addFirstRun("else")
                addFirstRun("   systemctl enable ssh")
                addFirstRun("fi")
            }

            if (generalTab.chkSetUser.checked) {
                /* Rename first ("pi") user if a different desired username was specified */
                addFirstRun("if [ -f /usr/lib/userconf-pi/userconf ]; then")
                addFirstRun("   /usr/lib/userconf-pi/userconf "+escapeshellarg(generalTab.fieldUserName.text)+" "+escapeshellarg(cryptedPassword))
                addFirstRun("else")
                addFirstRun("   echo \"$FIRSTUSER:\""+escapeshellarg(cryptedPassword)+" | chpasswd -e")
                addFirstRun("   if [ \"$FIRSTUSER\" != \""+generalTab.fieldUserName.text+"\" ]; then")
                addFirstRun("      usermod -l \""+generalTab.fieldUserName.text+"\" \"$FIRSTUSER\"")
                addFirstRun("      usermod -m -d \"/home/"+generalTab.fieldUserName.text+"\" \""+generalTab.fieldUserName.text+"\"")
                addFirstRun("      groupmod -n \""+generalTab.fieldUserName.text+"\" \"$FIRSTUSER\"")
                addFirstRun("      if grep -q \"^autologin-user=\" /etc/lightdm/lightdm.conf ; then")
                addFirstRun("         sed /etc/lightdm/lightdm.conf -i -e \"s/^autologin-user=.*/autologin-user="+generalTab.fieldUserName.text+"/\"")
                addFirstRun("      fi")
                addFirstRun("      if [ -f /etc/systemd/system/getty@tty1.service.d/autologin.conf ]; then")
                addFirstRun("         sed /etc/systemd/system/getty@tty1.service.d/autologin.conf -i -e \"s/$FIRSTUSER/"+generalTab.fieldUserName.text+"/\"")
                addFirstRun("      fi")
                addFirstRun("      if [ -f /etc/sudoers.d/010_pi-nopasswd ]; then")
                addFirstRun("         sed -i \"s/^$FIRSTUSER /"+generalTab.fieldUserName.text+" /\" /etc/sudoers.d/010_pi-nopasswd")
                addFirstRun("      fi")
                addFirstRun("   fi")
                addFirstRun("fi")
            }
            addCloudInit("")
        }
        if (generalTab.chkWifi.checked) {
            var wpaconfig = "country="+wifiCountry+"\n"
            wpaconfig += "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
            wpaconfig += "ap_scan=1\n\n"
            wpaconfig += "update_config=1\n"
            wpaconfig += "network={\n"
            if (generalTab.chkWifiSSIDHidden.checked) {
                wpaconfig += "\tscan_ssid=1\n"
            }
            wpaconfig += "\tssid=\""+generalTab.fieldWifiSSID.text+"\"\n"

            const isPassphrase = generalTab.fieldWifiPassword.text.length >= 8 &&
                generalTab.fieldWifiPassword.text.length < 64
            var cryptedPsk = isPassphrase ? imageWriter.pbkdf2(generalTab.fieldWifiPassword.text, generalTab.fieldWifiSSID.text)
                                          : generalTab.fieldWifiPassword.text
            wpaconfig += "\tpsk="+cryptedPsk+"\n"
            wpaconfig += "}\n"

            addFirstRun("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then")
            addFirstRun("   /usr/lib/raspberrypi-sys-mods/imager_custom set_wlan "
                        +(generalTab.chkWifiSSIDHidden.checked ? " -h " : "")
                        +escapeshellarg(generalTab.fieldWifiSSID.text)+" "+escapeshellarg(cryptedPsk)+" "+escapeshellarg(wifiCountry))
            addFirstRun("else")
            addFirstRun("cat >/etc/wpa_supplicant/wpa_supplicant.conf <<'WPAEOF'")
            addFirstRun(wpaconfig)
            addFirstRun("WPAEOF")
            addFirstRun("   chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf")
            addFirstRun("   rfkill unblock wifi")
            addFirstRun("   for filename in /var/lib/systemd/rfkill/*:wlan ; do")
            addFirstRun("       echo 0 > $filename")
            addFirstRun("   done")
            addFirstRun("fi")


            cloudinitnetwork  = "version: 2\n"
            cloudinitnetwork += "wifis:\n"
            cloudinitnetwork += "  renderer: networkd\n"
            cloudinitnetwork += "  wlan0:\n"
            cloudinitnetwork += "    dhcp4: true\n"
            cloudinitnetwork += "    optional: true\n"
            cloudinitnetwork += "    access-points:\n"
            cloudinitnetwork += "      \""+generalTab.fieldWifiSSID.text+"\":\n"
            cloudinitnetwork += "        password: \""+cryptedPsk+"\"\n"
            if (generalTab.chkWifiSSIDHidden.checked) {
                cloudinitnetwork += "        hidden: true\n"
            }

            addCmdline("cfg80211.ieee80211_regdom="+wifiCountry)
        }
        if (generalTab.chkLocale.checked) {
            var kbdconfig = "XKBMODEL=\"pc105\"\n"
            kbdconfig += "XKBLAYOUT=\""+keyboardLayout+"\"\n"
            kbdconfig += "XKBVARIANT=\"\"\n"
            kbdconfig += "XKBOPTIONS=\"\"\n"

            addFirstRun("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then")
            addFirstRun("   /usr/lib/raspberrypi-sys-mods/imager_custom set_keymap "+escapeshellarg(keyboardLayout))
            addFirstRun("   /usr/lib/raspberrypi-sys-mods/imager_custom set_timezone "+escapeshellarg(timezone))
            addFirstRun("else")
            addFirstRun("   rm -f /etc/localtime")
            addFirstRun("   echo \""+timezone+"\" >/etc/timezone")
            addFirstRun("   dpkg-reconfigure -f noninteractive tzdata")
            addFirstRun("cat >/etc/default/keyboard <<'KBEOF'")
            addFirstRun(kbdconfig)
            addFirstRun("KBEOF")
            addFirstRun("   dpkg-reconfigure -f noninteractive keyboard-configuration")
            addFirstRun("fi")

            addCloudInit("timezone: "+timezone)
            addCloudInit("keyboard:")
            addCloudInit("  model: pc105")
            addCloudInit("  layout: \"" + keyboardLayout + "\"")
        }

        if (firstrun.length) {
            firstrun = "#!/bin/bash\n\n"+"set +e\n\n"+firstrun
            addFirstRun("rm -f /boot/firstrun.sh")
            addFirstRun("sed -i 's| systemd.run.*||g' /boot/cmdline.txt")
            addFirstRun("exit 0")
            /* using systemd.run_success_action=none does not seem to have desired effect
               systemd then stays at "reached target kernel command line", so use reboot instead */
            //addCmdline("systemd.run=/boot/firstrun.sh systemd.run_success_action=reboot systemd.unit=kernel-command-line.target")
            // cmdline changing moved to DownloadThread::_customizeImage()
        }

        if (cloudinitwrite !== "") {
            addCloudInit("write_files:\n"+cloudinitwrite+"\n")
        }

        if (cloudinitrun !== "") {
            addCloudInit("runcmd:\n"+cloudinitrun+"\n")
        }

        imageWriter.setImageCustomization(config, cmdline, firstrun, cloudinit, cloudinitnetwork)
    }

    function saveSettings()
    {
        // Pre-trim ComboBox values to avoid repeated trim() calls
        var wifiCountry = generalTab.fieldWifiCountry.editText.trim()
        var timezone = generalTab.fieldTimezone.editText.trim()
        var keyboardLayout = generalTab.fieldKeyboardLayout.editText.trim()
        
        var settings = { };
        if (generalTab.chkHostname.checked && generalTab.fieldHostname.length) {
            settings.hostname = generalTab.fieldHostname.text
        }
        if (generalTab.chkSetUser.checked) {
            settings.sshUserName = generalTab.fieldUserName.text
            settings.sshUserPassword = generalTab.fieldUserPassword.alreadyCrypted ? generalTab.fieldUserPassword.text : imageWriter.crypt(generalTab.fieldUserPassword.text)
        }

        settings.sshEnabled = remoteAccessTab.chkSSH.checked
        if (remoteAccessTab.chkSSH.checked && remoteAccessTab.radioPubKeyAuthentication.checked) {
            var publicKeysSerialised = ""
            for (var j=0; j<remoteAccessTab.publicKeyModel.count; j++) {
                var pkitem = remoteAccessTab.publicKeyModel.get(j)["publicKeyField"].trim()
                if (pkitem) {
                    publicKeysSerialised += pkitem +"\n"
                }
            }

            settings.sshAuthorizedKeys = publicKeysSerialised
        }
        if (generalTab.chkWifi.checked) {
            settings.wifiSSID = generalTab.fieldWifiSSID.text
            if (generalTab.chkWifiSSIDHidden.checked) {
                settings.wifiSSIDHidden = true
            }
            settings.wifiCountry = wifiCountry

            const isPassphrase = generalTab.fieldWifiPassword.text.length >= 8 &&
                generalTab.fieldWifiPassword.text.length < 64
            var cryptedPsk = isPassphrase ? imageWriter.pbkdf2(generalTab.fieldWifiPassword.text, generalTab.fieldWifiSSID.text)
                                          : generalTab.fieldWifiPassword.text
            settings.wifiPassword = cryptedPsk
        }
        if (generalTab.chkLocale.checked) {
            settings.timezone = timezone
            settings.keyboardLayout = keyboardLayout
        }

        imageWriter.setSetting("beep", optionsTab.beepEnabled)
        imageWriter.setSetting("eject", optionsTab.ejectEnabled)
        imageWriter.setSetting("telemetry", optionsTab.telemetryEnabled)

        if (generalTab.chkHostname.checked || generalTab.chkSetUser.checked || remoteAccessTab.chkSSH.checked || generalTab.chkWifi.checked || generalTab.chkLocale.checked) {
            /* OS customization to be applied. */
            hasSavedSettings = true
            saveSettingsSignal(settings)
        }
    }

    function restorePreviousState() {
        // Restore UI to the last saved state by reloading from persistent settings
        // This discards any unsaved changes made by the user
        
        // Clear current state first
        clearCustomizationFields()
        
        // Reload from saved settings if they exist
        if (imageWriter.hasSavedCustomizationSettings()) {
            loadSettingsIntoUI()
        }
        
        // Reset the session flag
        changesAppliedInSession = false
    }
    
    function loadSettingsIntoUI() {
        // Load saved settings into the UI (similar to initialize() but focused on settings restoration)
        var settings = imageWriter.getSavedCustomizationSettings()
        
        if ('hostname' in settings) {
            generalTab.fieldHostname.text = settings.hostname
            generalTab.chkHostname.checked = true
        }
        if ('sshUserPassword' in settings) {
            generalTab.fieldUserPassword.text = settings.sshUserPassword
            generalTab.fieldUserPassword.alreadyCrypted = true
            generalTab.chkSetUser.checked = true
            if (!('sshEnabled' in settings) || settings.sshEnabled === "true" || settings.sshEnabled === true) {
                remoteAccessTab.chkSSH.checked = true
                remoteAccessTab.radioPasswordAuthentication.checked = true
            }
        }
        if ('sshAuthorizedKeys' in settings) {
            var possiblePublicKeys = settings.sshAuthorizedKeys.split('\n')
            // Clear existing keys first
            remoteAccessTab.publicKeyModel.clear()
            
            for (const publicKey of possiblePublicKeys) {
                var pkitem = publicKey.trim()
                if (pkitem) {
                    remoteAccessTab.publicKeyModel.append({publicKeyField: pkitem})
                }
            }

            remoteAccessTab.radioPubKeyAuthentication.checked = true
            remoteAccessTab.chkSSH.checked = true
        }
        
        if ('sshUserName' in settings) {
            generalTab.fieldUserName.text = settings.sshUserName
            generalTab.chkSetUser.checked = true
        }

        if ('wifiSSID' in settings) {
            generalTab.fieldWifiSSID.text = settings.wifiSSID
            if ('wifiSSIDHidden' in settings && settings.wifiSSIDHidden) {
                generalTab.chkWifiSSIDHidden.checked = true
            }
            generalTab.fieldWifiPassword.text = settings.wifiPassword
            generalTab.fieldWifiCountry.currentIndex = generalTab.fieldWifiCountry.find(settings.wifiCountry)
            if (generalTab.fieldWifiCountry.currentIndex == -1) {
                generalTab.fieldWifiCountry.editText = settings.wifiCountry
            }
            generalTab.chkWifi.checked = true
        }

        if ('timezone' in settings) {
            generalTab.chkLocale.checked = true
            var tzidx = generalTab.fieldTimezone.find(settings.timezone)
            if (tzidx === -1) {
                generalTab.fieldTimezone.editText = settings.timezone
            } else {
                generalTab.fieldTimezone.currentIndex = tzidx
            }
        }
        if ('keyboardLayout' in settings) {
            generalTab.fieldKeyboardLayout.currentIndex = generalTab.fieldKeyboardLayout.find(settings.keyboardLayout)
            if (generalTab.fieldKeyboardLayout.currentIndex == -1) {
                generalTab.fieldKeyboardLayout.editText = settings.keyboardLayout
            }
        }
        
        // Attempt to insert the default public key, but avoid clashes.
        // This ensures the default key is available even when restoring settings
        var insertDefaultKey = true
        var defaultKey = imageWriter.getDefaultPubKey()
        if (defaultKey && defaultKey.length > 0) {
            for (var i = 0; i < remoteAccessTab.publicKeyModel.count; i++) {
                if (remoteAccessTab.publicKeyModel.get(i)["publicKeyField"] == defaultKey) {
                    insertDefaultKey = false
                    break;
                }
            }
            if (insertDefaultKey) {
                remoteAccessTab.publicKeyModel.append({publicKeyField: defaultKey})
            }
        }
        
        // Force final ListView refresh after all keys (including default) are loaded
        remoteAccessTab.forceListViewRefresh()
    }

    function clearCustomizationFields()
    {
        /* Bind copies of the lists */
        generalTab.fieldTimezone.model = imageWriter.getTimezoneList()
        generalTab.fieldKeyboardLayout.model = imageWriter.getKeymapLayoutList()
        generalTab.fieldWifiCountry.model = imageWriter.getCountryList()

        generalTab.fieldHostname.text = "raspberrypi"
        generalTab.fieldUserName.text = imageWriter.getCurrentUser()
        generalTab.fieldUserPassword.clear()
        remoteAccessTab.radioPubKeyAuthentication.checked = false
        remoteAccessTab.radioPasswordAuthentication.checked = false
        remoteAccessTab.publicKeyModel.clear()
        
        // Add default key if available (when clearing, we want to show the default key)
        var defaultKey = imageWriter.getDefaultPubKey()
        if (defaultKey && defaultKey.length > 0) {
            remoteAccessTab.publicKeyModel.append({publicKeyField: defaultKey})
        }
        
        // Force ListView refresh after clearing and adding default key
        remoteAccessTab.forceListViewRefresh()

        /* Timezone Settings*/
        generalTab.fieldTimezone.currentIndex = generalTab.fieldTimezone.find(imageWriter.getTimezone())
        /* Lacking an easy cross-platform to fetch keyboard layout
            from host system, just default to "gb" for people in
            UK time zone for now, and "us" for everyone else */
        if (imageWriter.getTimezone() === "Europe/London") {
            generalTab.fieldKeyboardLayout.currentIndex = generalTab.fieldKeyboardLayout.find("gb")
        } else {
            generalTab.fieldKeyboardLayout.currentIndex = generalTab.fieldKeyboardLayout.find("us")
        }

        generalTab.chkSetUser.checked = false
        remoteAccessTab.chkSSH.checked = false
        generalTab.chkLocale.checked = false
        generalTab.chkWifiSSIDHidden.checked = false
        generalTab.chkHostname.checked = false

        /* WiFi Settings */
        generalTab.fieldWifiSSID.text = imageWriter.getSSID()
        if (generalTab.fieldWifiSSID.text.length) {
            generalTab.fieldWifiPassword.text = imageWriter.getPSK()
            if (generalTab.fieldWifiPassword.text.length) {
                if (Qt.platform.os == "osx") {
                    /* User indicated wifi must be prefilled */
                    generalTab.chkWifi.checked = true
                }
            }
        }
        
        // Reset the session flag when clearing fields
        changesAppliedInSession = false
    }
}
