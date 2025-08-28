/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

import RpiImager

WizardStepBase {
    id: root
    
    required property ImageWriter imageWriter
    required property var wizardContainer
    
    title: qsTr("Write Image")
    subtitle: qsTr("Review your choices and write the image to the storage device")
    nextButtonText: root.isWriting && root.isVerifying ? qsTr("Skip verification") : (root.isWriting ? qsTr("Cancel") : (root.isComplete ? qsTr("Continue") : qsTr("Write")))
    nextButtonEnabled: true
    showBackButton: true
    
    property bool isWriting: false
    property bool isComplete: false
    property bool isVerifying: false
    property bool confirmOpen: false
    readonly property bool anyCustomizationsApplied: (
        wizardContainer.hostnameConfigured ||
        wizardContainer.localeConfigured ||
        wizardContainer.userConfigured ||
        wizardContainer.wifiConfigured ||
        wizardContainer.sshEnabled ||
        wizardContainer.featPiConnectEnabled
    )
    
    // Disable back while writing
    backButtonEnabled: !root.isWriting

    // Content
    content: [
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.cardPadding
        spacing: Style.spacingLarge

        // Top spacer to vertically center progress section when writing/complete
        Item { Layout.fillHeight: true; visible: root.isWriting || root.isComplete }
        
        // Summary section (de-chromed)
        ColumnLayout {
            id: summaryLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Style.sectionMaxWidth
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.spacingMedium
            visible: !root.isWriting

            Text {
                text: qsTr("Summary")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
            }
            
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Style.formColumnSpacing
                rowSpacing: Style.spacingSmall
                
                Text {
                    text: qsTr("Device:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                }
                
                Text {
                    text: wizardContainer.selectedDeviceName || qsTr("No device selected")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
                
                Text {
                    text: qsTr("Operating System:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                }
                
                Text {
                    text: wizardContainer.selectedOsName || qsTr("No image selected")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
                
                Text {
                    text: qsTr("Storage:")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamily
                    color: Style.formLabelColor
                }
                
                Text {
                    text: wizardContainer.selectedStorageName || qsTr("No storage selected")
                    font.pixelSize: Style.fontSizeDescription
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.formLabelColor
                    Layout.fillWidth: true
                }
            }
            
            Text {
                text: root.anyCustomizationsApplied 
                      ? qsTr("Ready to write your customized image to the storage device. All existing data will be erased.")
                      : qsTr("Ready to write the image to the storage device. All existing data will be erased.")
                font.pixelSize: Style.fontSizeDescription
                font.family: Style.fontFamily
                color: Style.textDescriptionColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        
        // Customization summary (what will be written) - de-chromed
        ColumnLayout {
            id: customLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Style.sectionMaxWidth
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.spacingMedium
            visible: !root.isWriting && root.anyCustomizationsApplied

            Text {
                text: qsTr("Customizations to apply:")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
            }

            Column {
                Layout.fillWidth: true
                spacing: Style.spacingXSmall

                Text { text: qsTr("• Hostname configured");        font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.hostnameConfigured }
                Text { text: qsTr("• User account configured");    font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.userConfigured }
                Text { text: qsTr("• WiFi configured");            font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.wifiConfigured }
                Text { text: qsTr("• SSH enabled");                font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.sshEnabled }
                Text { text: qsTr("• Pi Connect enabled"); font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor; visible: wizardContainer.piConnectEnabled }
                Text { text: qsTr("• Locale configured");         font.pixelSize: Style.fontSizeDescription; font.family: Style.fontFamily; color: Style.formLabelColor;     visible: wizardContainer.localeConfigured }
            }
        }

        // Progress section (de-chromed)
        ColumnLayout {
            id: progressLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Style.sectionMaxWidth
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.spacingMedium
            visible: root.isWriting || root.isComplete
            
            Text {
                id: progressText
                text: qsTr("Starting write process...")
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }
            
            ProgressBar {
                id: progressBar
                Layout.fillWidth: true
                Layout.preferredHeight: Style.spacingLarge
                value: 0
                from: 0
                to: 100
                
                // Default to write color; updated dynamically in onDownloadProgress/onVerifyProgress
                Material.accent: Style.progressBarWritingForegroundColor
                Material.background: Style.progressBarTrackColor
                visible: root.isWriting
            }

            // Separate skip button removed; main Next button switches label during verification
        }

        // Bottom spacer to vertically center progress section when writing/complete
        Item { Layout.fillHeight: true; visible: root.isWriting || root.isComplete }
    }
    ]
    
    // Handle next button clicks based on current state
    onNextClicked: {
        if (root.isWriting) {
            // Cancel the write
            if (root.isVerifying) {
                imageWriter.skipCurrentVerification()
            } else {
                imageWriter.cancelWrite()
                root.isWriting = false
                wizardContainer.isWriting = false
                progressText.text = qsTr("Write cancelled")
            }
        } else if (!root.isComplete) {
            // If warnings are disabled, skip the confirmation dialog
            if (imageWriter.getBoolSetting("disable_warnings")) {
                beginWriteDelay.start()
            } else {
                // Open confirmation dialog before starting
                confirmDialog.open()
            }
        } else {
            // Writing is complete, advance to next step
            wizardContainer.nextStep()
        }
    }
    
    function onFinalizing() {
        root.isVerifying = false
        progressText.text = qsTr("Finalizing...")
        progressBar.value = 100
    }

    // Confirmation dialog
    Dialog {
        id: confirmDialog
        modal: true
        parent: wizardContainer && wizardContainer.overlayRootRef ? wizardContainer.overlayRootRef : undefined
        anchors.centerIn: parent
        width: 520
        standardButtons: Dialog.NoButton
        visible: false

        property bool allowAccept: false

        background: Rectangle {
            color: Style.mainBackgroundColor
            radius: Style.sectionBorderRadius
            border.color: Style.popupBorderColor
            border.width: Style.sectionBorderWidth
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.cardPadding
            spacing: Style.spacingMedium

            Text {
                text: qsTr("You are about to ERASE all data on: %1").arg(wizardContainer.selectedStorageName || qsTr("the storage device"))
                font.pixelSize: Style.fontSizeHeading
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelErrorColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("This action is PERMANENT and CANNOT be undone.")
                font.pixelSize: Style.fontSizeFormLabel
                font.family: Style.fontFamilyBold
                color: Style.formLabelColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.spacingMedium
                Item { Layout.fillWidth: true }

                ImButton {
                    text: qsTr("Cancel")
                    onClicked: confirmDialog.close()
                }

                ImButtonRed {
                    id: acceptBtn
                    text: confirmDialog.allowAccept ? qsTr("I understand, erase and write") : qsTr("Please wait...")
                    enabled: confirmDialog.allowAccept
                    onClicked: {
                        confirmDialog.close()
                        beginWriteDelay.start()
                    }
                }
            }
        }

        // Delay accept for 2 seconds
        Timer {
            id: confirmDelay
            interval: 2000
            running: false
            repeat: false
            onTriggered: confirmDialog.allowAccept = true
        }

        // Ensure disabled before showing to avoid flicker
        onAboutToShow: {
            allowAccept = false
            confirmDelay.start()
        }
        onClosed: {
            confirmDelay.stop()
            allowAccept = false
        }
    }

    // Defer starting the write slightly until after the dialog has fully closed,
    // to avoid OS authentication prompts being cancelled by focus changes.
    Timer {
        id: beginWriteDelay
        interval: 300
        running: false
        repeat: false
        onTriggered: {
            // Ensure our window regains focus before elevating privileges
            root.forceActiveFocus()
            root.isWriting = true
            wizardContainer.isWriting = true
            progressText.text = qsTr("Starting write process...")
            progressBar.value = 0
            Qt.callLater(function(){ imageWriter.startWrite() })
        }
    }

    // --- helpers to build customization payloads ---
    function _escapeshellarg(arg) {
        return "'" + String(arg).replace(/'/g, "'\"'\"'") + "'"
    }
    function _acc(s, line) { return s + line + "\n" }

    // Build cloud-init and first-boot content from wizardContainer and push to imageWriter
    function applyCustomizationsFromWizard() {
        // Read inputs from the wizard
        const hostEnabled = !!wizardContainer.hostnameConfigured
        const hostName    = wizardContainer.hostname                   || ""
        const userEnabled = !!wizardContainer.userConfigured
        const userName    = wizardContainer.userName                   || imageWriter.getCurrentUser()
        const userPass    = wizardContainer.userPassword               || ""    // plain or crypted
        const userPassCrypted = !!wizardContainer.userPasswordCrypted
        const sshEnabled  = !!wizardContainer.sshEnabled
        const sshAuth     = wizardContainer.sshAuth                    || "password"   // "keys" | "password"
        const sshKeys     = wizardContainer.sshPublicKeys              || []           // array of strings

        const wifiEnabled = !!wizardContainer.wifiConfigured
        const wifiSsid    = wizardContainer.wifiSsid                   || ""
        const wifiPass    = wizardContainer.wifiPassword               || ""
        const wifiHidden  = !!wizardContainer.wifiHidden
        const wifiCountry = wizardContainer.wifiCountry                || "GB"

        const locEnabled  = !!wizardContainer.localeConfigured
        const timezone    = wizardContainer.timezone                   || imageWriter.getTimezone()
        const kbdLayout   = wizardContainer.keyboardLayout             || "us"

        const ifI2CEnabled    = !!wizardContainer.ifI2cEnabled
        const ifSpiEnabled    = !!wizardContainer.ifSpiEnabled
        const ifSerialEnabled = !!wizardContainer.ifSerialEnabled
        const ifSerialMode    = !!wizardContainer.ifSerialMode
        const featPiConnect   = !!wizardContainer.featPiConnectEnabled

        // Buffers
        let config = ""                  // (left empty unless you need /boot/config.txt edits)
        let cmdline = ""                 // kernel cmdline additions (we add wifi regdom)
        let firstrun = ""                // /boot/firstrun.sh content (executed once)
        let cloudinit = ""               // cloud-init user-data
        let cloudinitnetwork = ""        // cloud-init network config
        let cloudinitwrite = ""          // extra write_files (kept for future)
        let cloudinitrun = ""            // extra runcmd (kept for future)

        function addFirstRun(s)     { firstrun = _acc(firstrun, s) }
        function addCloudInit(s)    { cloudinit = _acc(cloudinit, s) }
        function addCloudNet(s)     { cloudinitnetwork = _acc(cloudinitnetwork, s) }
        function addCmdline(s)      { cmdline += " " + s }

        const isRpiosCloudInit = imageWriter.checkSWCapability("rpios_cloudinit")

        // Hostname
        if (hostEnabled && hostName) {
            addFirstRun('CURRENT_HOSTNAME=`cat /etc/hostname | tr -d " \\t\\n\\r"`')
            addFirstRun('if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then')
            addFirstRun('   /usr/lib/raspberrypi-sys-mods/imager_custom set_hostname ' + hostName)
            addFirstRun('else')
            addFirstRun('   echo ' + hostName + ' >/etc/hostname')
            addFirstRun('   sed -i "s/127.0.1.1.*$CURRENT_HOSTNAME/127.0.1.1\\t' + hostName + '/g" /etc/hosts')
            addFirstRun('fi')

            addCloudInit('hostname: ' + hostName)
            addCloudInit('manage_etc_hosts: true')
            addCloudInit('packages:')
            addCloudInit('- avahi-daemon')
            // apt date check can fail before NTP sync — disable
            addCloudInit('apt:')
            addCloudInit('  conf: |')
            addCloudInit('    Acquire {')
            addCloudInit('      Check-Date "false";')
            addCloudInit('    };')
            addCloudInit('')
        }

        // User + SSH
        if (sshEnabled || userEnabled) {
            addFirstRun('FIRSTUSER=`getent passwd 1000 | cut -d: -f1`')
            addFirstRun('FIRSTUSERHOME=`getent passwd 1000 | cut -d: -f6`')

            // users:
            addCloudInit('users:')
            addCloudInit('- name: ' + userName)
            addCloudInit('  groups: users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo')
            addCloudInit('  shell: /bin/bash')

            let cryptedPassword = ""
            if (userEnabled) {
                cryptedPassword = userPassCrypted ? userPass : imageWriter.crypt(userPass)
                addCloudInit('  lock_passwd: false')
                addCloudInit('  passwd: ' + cryptedPassword)
            }

            if (sshEnabled && sshAuth === "keys" && sshKeys.length > 0) {
                // First-boot path
                let spaceSep = ""
                let newlineSep = ""
                for (let i = 0; i < sshKeys.length; ++i) {
                    const pk = String(sshKeys[i]).trim()
                    if (!pk) continue
                    spaceSep   += " " + _escapeshellarg(pk)
                    newlineSep += pk + "\\n"
                }

                addFirstRun('if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then')
                addFirstRun('   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh -k' + spaceSep)
                addFirstRun('else')
                addFirstRun('   install -o "$FIRSTUSER" -m 700 -d "$FIRSTUSERHOME/.ssh"')
                addFirstRun('   install -o "$FIRSTUSER" -m 600 <(printf "' + newlineSep + '") "$FIRSTUSERHOME/.ssh/authorized_keys"')
                addFirstRun("   echo 'PasswordAuthentication no' >>/etc/ssh/sshd_config")
                addFirstRun('   systemctl enable ssh')
                addFirstRun('fi')

                if (!userEnabled) addCloudInit('  lock_passwd: true')
                addCloudInit('  ssh_authorized_keys:')
                for (let i = 0; i < sshKeys.length; ++i) {
                    const pk = String(sshKeys[i]).trim()
                    if (pk) addCloudInit('    - ' + pk)
                }
                addCloudInit('  sudo: ALL=(ALL) NOPASSWD:ALL')
            }

            if (sshEnabled && sshAuth === "password") {
                addCloudInit('ssh_pwauth: true')
                addFirstRun('if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then')
                addFirstRun('   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh')
                addFirstRun('else')
                addFirstRun("   echo 'PasswordAuthentication yes' >>/etc/ssh/sshd_config")
                addFirstRun('   systemctl enable ssh')
                addFirstRun('fi')
            }

            if (userEnabled) {
                // rename first user if needed
                addFirstRun('if [ -f /usr/lib/userconf-pi/userconf ]; then')
                addFirstRun('   /usr/lib/userconf-pi/userconf ' + _escapeshellarg(userName) + ' ' + _escapeshellarg(cryptedPassword))
                addFirstRun('else')
                addFirstRun('   echo "$FIRSTUSER:' + _escapeshellarg(cryptedPassword) + ' | chpasswd -e"')
                addFirstRun('   if [ "$FIRSTUSER" != "' + userName + '" ]; then')
                addFirstRun('      usermod -l "' + userName + '" "$FIRSTUSER"')
                addFirstRun('      usermod -m -d "/home/' + userName + '" "' + userName + '"')
                addFirstRun('      groupmod -n "' + userName + '" "$FIRSTUSER"')
                addFirstRun('      if grep -q "^autologin-user=" /etc/lightdm/lightdm.conf ; then')
                addFirstRun('         sed /etc/lightdm/lightdm.conf -i -e "s/^autologin-user=.*/autologin-user=' + userName + '/"')
                addFirstRun('      fi')
                addFirstRun('      if [ -f /etc/systemd/system/getty@tty1.service.d/autologin.conf ]; then')
                addFirstRun('         sed /etc/systemd/system/getty@tty1.service.d/autologin.conf -i -e "s/$FIRSTUSER/' + userName + '/"')
                addFirstRun('      fi')
                addFirstRun('      if [ -f /etc/sudoers.d/010_pi-nopasswd ]; then')
                addFirstRun('         sed -i "s/^$FIRSTUSER /' + userName + ' /" /etc/sudoers.d/010_pi-nopasswd')
                addFirstRun('      fi')
                addFirstRun('   fi')
                addFirstRun('fi')
            }

            addCloudInit('')
        }

        // cc_raspberry_pi config for rpios_cloudinit capable OSs
        if (isRpiosCloudInit && (piConnect /* || i2cEnabled */)) {
            addCloudInit("rpi:")
            if (featPiConnect) {
                addCloudInit("  enable_rpi_connect: true")
            }

            // configure ARM interfaces
            if (ifI2CEnabled || ifSpiEnabled || ifSerialEnabled) {
                addCloudInit("  interfaces:")
                if (ifI2CEnabled) {
                    addCloudInit("    i2c: true")
                }
                if (ifSpiEnabled) {
                    addCloudInit("    spi: true")
                }
                if (ifSerialEnabled) {
                    if (ifSerialMode === "" || ifSerialMode === "default") {
                        addCloudInit("    serial: true")
                    } else {
                        addCloudInit("    serial:")
                        switch (ifSerialMode) {
                        case "console_hw":
                            addCloudInit("      console: true")
                            addCloudInit("      hardware: true")
                            break;
                        case "console":
                            addCloudInit("      console: true")
                            addCloudInit("      hardware: false")
                            break;
                        case "hardware":
                            addCloudInit("      console: false")
                            addCloudInit("      hardware: true")
                            break;
                        default:
                            console.debug("Invalid ifSerialMode:", ifSerialMode)
                            break;
                        }
                    }
                }
            }
        }

        // Wi-Fi
        if (wifiEnabled && wifiSsid) {
            // wpa_supplicant
            let wpaconfig  = "country=" + wifiCountry + "\n"
            wpaconfig += "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
            wpaconfig += "ap_scan=1\n\n"
            wpaconfig += "update_config=1\n"
            wpaconfig += "network={\n"
            if (wifiHidden) wpaconfig += "\tscan_ssid=1\n"
            wpaconfig += "\tssid=\"" + wifiSsid + "\"\n"

            const isPassphrase = wifiPass.length >= 8 && wifiPass.length < 64
            const cryptedPsk = isPassphrase ? imageWriter.pbkdf2(wifiPass, wifiSsid) : wifiPass
            wpaconfig += "\tpsk=" + cryptedPsk + "\n"
            wpaconfig += "}\n"

            addFirstRun('if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then')
            addFirstRun('   /usr/lib/raspberrypi-sys-mods/imager_custom set_wlan '
                + (wifiHidden ? ' -h ' : '')
                + _escapeshellarg(wifiSsid) + ' ' + _escapeshellarg(cryptedPsk) + ' ' + _escapeshellarg(wifiCountry))
            addFirstRun('else')
            addFirstRun("cat >/etc/wpa_supplicant/wpa_supplicant.conf <<'WPAEOF'")
            addFirstRun(wpaconfig)
            addFirstRun("WPAEOF")
            addFirstRun('   chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf')
            addFirstRun('   rfkill unblock wifi')
            addFirstRun('   for filename in /var/lib/systemd/rfkill/*:wlan ; do')
            addFirstRun('       echo 0 > $filename')
            addFirstRun('   done')
            addFirstRun('fi')

            // cloud-init network
            addCloudNet('network:')
            addCloudNet('  version: 2')
            addCloudNet('  renderer: ' + (isRpiosCloudInit ? 'NetworkManager' : 'networkd'))
            addCloudNet('  wifis:')
            addCloudNet('    wlan0:')
            addCloudNet('      dhcp4: true')
            addCloudNet('      dhcp6: true')
            addCloudNet('      optional: true')
            addCloudNet('      access-points:')
            addCloudNet('        "' + wifiSsid + '":')
            addCloudNet('          password: "' + cryptedPsk + '"')
            if (wifiHidden) addCloudNet('          hidden: true')

            // regulatory domain
            addCmdline('cfg80211.ieee80211_regdom=' + wifiCountry)
        }

        // Locale (timezone + keyboard)
        if (locEnabled) {
            const kbdconfig =
                'XKBMODEL="pc105"\n' +
                'XKBLAYOUT="' + kbdLayout + '"\n' +
                'XKBVARIANT=""\n' +
                'XKBOPTIONS=""\n'

            addFirstRun('if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then')
            addFirstRun('   /usr/lib/raspberrypi-sys-mods/imager_custom set_keymap ' + _escapeshellarg(kbdLayout))
            addFirstRun('   /usr/lib/raspberrypi-sys-mods/imager_custom set_timezone ' + _escapeshellarg(timezone))
            addFirstRun('else')
            addFirstRun('   rm -f /etc/localtime')
            addFirstRun('   echo "' + timezone + '" >/etc/timezone')
            addFirstRun('   dpkg-reconfigure -f noninteractive tzdata')
            addFirstRun("cat >/etc/default/keyboard <<'KBEOF'")
            addFirstRun(kbdconfig)
            addFirstRun('KBEOF')
            addFirstRun('   dpkg-reconfigure -f noninteractive keyboard-configuration')
            addFirstRun('fi')

            addCloudInit('timezone: ' + timezone)
            addCloudInit('keyboard:')
            addCloudInit('  model: pc105')
            addCloudInit('  layout: "' + kbdLayout + '"')
        }

        // Finalize firstrun wrapper and cloud-init extras
        if (firstrun.length) {
            firstrun = "#!/bin/bash\n\nset +e\n\n" + firstrun
            addFirstRun('rm -f /boot/firstrun.sh')
            addFirstRun("sed -i 's| systemd.run.*||g' /boot/cmdline.txt")
            addFirstRun('exit 0')
        }
        if (cloudinitwrite) addCloudInit("write_files:\n" + cloudinitwrite + "\n")
        if (cloudinitrun)   addCloudInit("runcmd:\n" + cloudinitrun + "\n")

        // Push to ImageWriter (always call to clear any stale state)
        imageWriter.setImageCustomization(
            config,
            cmdline.trim(),
            firstrun,
            cloudinit,
            cloudinitnetwork,
            false
        )
    }
    
    function onDownloadProgress(now, total) {
        if (root.isWriting) {
            var progress = total > 0 ? (now / total) * 100 : 0
            root.isVerifying = false
            progressBar.value = progress
            progressText.text = qsTr("Writing... %1%").arg(Math.round(progress))
            progressBar.Material.accent = Style.progressBarWritingForegroundColor
        }
    }
    
    function onVerifyProgress(now, total) {
        if (root.isWriting) {
            var progress = total > 0 ? (now / total) * 100 : 0
            root.isVerifying = true
            progressBar.value = progress
            progressText.text = qsTr("Verifying... %1%").arg(Math.round(progress))
            progressBar.Material.accent = Style.progressBarVerifyForegroundColor
        }
    }
    
    function onPreparationStatusUpdate(msg) {
        if (root.isWriting) {
            progressText.text = msg
        }
    }
    
    // Update isWriting state when write completes
    Connections {
        target: imageWriter
        function onSuccess() {
            root.isWriting = false
            wizardContainer.isWriting = false
            root.isComplete = true
            progressText.text = qsTr("Write completed successfully!")
            
            // Automatically advance to the done screen
            wizardContainer.nextStep()
        }
        function onError(msg) {
            root.isWriting = false
            wizardContainer.isWriting = false
            progressText.text = qsTr("Write failed: %1").arg(msg)
        }

        function onFinalizing() {
            if (root.isWriting) {
                root.onFinalizing()
            }
        }
    }
} 
