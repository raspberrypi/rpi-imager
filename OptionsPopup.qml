/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2
import "qmlcomponents"

Popup {
    id: popup
    //x: 62
    x: (parent.width-width)/2
    y: 10
    //width: parent.width-125
    width: popupbody.implicitWidth+60
    height: parent.height-20
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    property bool initialized: false
    property bool hasSavedSettings: false
    property string config
    property string cmdline
    property string firstrun
    property string cloudinit
    property string cloudinitrun
    property string cloudinitwrite
    property string cloudinitnetwork

    // background of title
    Rectangle {
        color: "#f5f5f5"
        anchors.right: parent.right
        anchors.top: parent.top
        height: 35
        width: parent.width
    }
    // line under title
    Rectangle {
        color: "#afafaf"
        width: parent.width
        y: 35
        implicitHeight: 1
    }

    Text {
        id: msgx
        text: "X"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 25
        anchors.topMargin: 10
        font.family: roboto.name
        font.bold: true

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                popup.close()
            }
        }
    }

    ColumnLayout {
        spacing: 20
        anchors.fill: parent

        Text {
            id: popupheader
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
            Layout.topMargin: 10
            font.family: roboto.name
            font.bold: true
            text: qsTr("Advanced options")
        }

        ScrollView {
            id: popupbody
            font.family: roboto.name
            //Layout.maximumWidth: popup.width-30
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 25
            Layout.topMargin: 10
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn

          ColumnLayout {

            GroupBox {
                title: qsTr("Image customization options")
                label: RowLayout {
                    Label {
                        text: parent.parent.title
                    }
                    ComboBox {
                        id: comboSaveSettings
                        model: {
                            [qsTr("for this session only"),
                             qsTr("to always use")]
                        }
                        Layout.minimumWidth: 250
                        Layout.maximumHeight: 40
                        enabled: !imageWriter.isEmbeddedMode()
                    }
                }

                ColumnLayout {
                    spacing: -10

                    ImCheckBox {
                        id: chkOverscan
                        text: qsTr("Disable overscan")
                    }
                    RowLayout {
                        ImCheckBox {
                            id: chkHostname
                            text: qsTr("Set hostname:")
                            onCheckedChanged: {
                                if (checked) {
                                    fieldHostname.forceActiveFocus()
                                }
                            }
                        }
                        TextField {
                            id: fieldHostname
                            enabled: chkHostname.checked
                            text: "raspberrypi"
                        }
                        Text {
                            text : ".local"
                            color: chkHostname.checked ? "black" : "grey"
                        }
                    }
                    ImCheckBox {
                        id: chkSSH
                        text: qsTr("Enable SSH")
                        onCheckedChanged: {
                            if (checked) {
                                if (!radioPasswordAuthentication.checked && !radioPubKeyAuthentication.checked) {
                                    radioPasswordAuthentication.checked = true
                                }
                                if (radioPasswordAuthentication.checked) {
                                    chkSetUser.checked = true
                                    if (!fieldUserPassword.length) {
                                        fieldUserPassword.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        enabled: chkSSH.checked
                        Layout.leftMargin: 40
                        spacing: -10

                        ImRadioButton {
                            id: radioPasswordAuthentication
                            text: qsTr("Use password authentication")
                            onCheckedChanged: {
                                if (checked) {
                                    chkSetUser.checked = true
                                    fieldUserPassword.forceActiveFocus()
                                }
                            }
                        }
                        ImRadioButton {
                            id: radioPubKeyAuthentication
                            text: qsTr("Allow public-key authentication only")
                            onCheckedChanged: {
                                if (checked) {
                                    fieldPublicKey.forceActiveFocus()
                                }
                            }
                        }
                        GridLayout {
                            Layout.leftMargin: 40
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: -5
                            enabled: radioPubKeyAuthentication.checked

                            Text {
                                text: qsTr("Set authorized_keys for '%1':").arg(fieldUserName.text)
                                color: parent.enabled ? "black" : "grey"
                            }
                            TextField {
                                id: fieldPublicKey
                                Layout.minimumWidth: 200
                            }
                        }
                    }

                    ImCheckBox {
                        id: chkSetUser
                        text: qsTr("Set username and password")
                        onCheckedChanged: {
                            if (!checked && chkSSH.checked && radioPasswordAuthentication.checked) {
                                checked = true;
                            }
                        }
                    }

                    ColumnLayout {
                        enabled: chkSetUser.checked
                        Layout.leftMargin: 40
                        spacing: -5

                        GridLayout {
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: -5

                            Text {
                                text: qsTr("Username:")
                                color: parent.enabled ? (fieldUserName.indicateError ? "red" : "black") : "grey"
                            }
                            TextField {
                                id: fieldUserName
                                text: "pi"
                                Layout.minimumWidth: 200
                                property bool indicateError: false

                                onTextEdited: {
                                    indicateError = false
                                }
                            }

                            Text {
                                text: qsTr("Password:")
                                color: parent.enabled ? (fieldUserPassword.indicateError ? "red" : "black") : "grey"
                            }
                            TextField {
                                id: fieldUserPassword
                                echoMode: TextInput.Password
                                Layout.minimumWidth: 200
                                property bool alreadyCrypted: false
                                property bool indicateError: false

                                onTextEdited: {
                                    if (alreadyCrypted) {
                                        /* User is trying to edit saved
                                           (crypted) password, clear field */
                                        alreadyCrypted = false
                                        clear()
                                    }
                                    if (indicateError) {
                                        indicateError = false
                                    }
                                }
                            }
                        }
                    }

                    ImCheckBox {
                        id: chkWifi
                        text: qsTr("Configure wifi")
                        onCheckedChanged: {
                            if (checked) {
                                if (!fieldWifiSSID.length) {
                                    fieldWifiSSID.forceActiveFocus()
                                } else if (!fieldWifiPassword.length) {
                                    fieldWifiPassword.forceActiveFocus()
                                }
                            }
                        }
                    }
                    GridLayout {
                        enabled: chkWifi.checked
                        Layout.leftMargin: 40
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: -5

                        Text {
                            text: qsTr("SSID:")
                            color: parent.enabled ? (fieldWifiSSID.indicateError ? "red" : "black") : "grey"
                        }
                        TextField {
                            id: fieldWifiSSID
                            Layout.minimumWidth: 200
                            property bool indicateError: false
                            onTextEdited: {
                                indicateError = false
                            }
                        }

                        ImCheckBox {
                            id: chkWifiSSIDHidden
                            Layout.columnSpan: 2
                            text: qsTr("Hidden SSID")
                            checked: false
                        }

                        Text {
                            text: qsTr("Password:")
                            color: parent.enabled ? (fieldWifiPassword.indicateError ? "red" : "black") : "grey"
                        }
                        TextField {
                            id: fieldWifiPassword
                            Layout.minimumWidth: 200
                            echoMode: chkShowPassword.checked ? TextInput.Normal : TextInput.Password
                            property bool indicateError: false
                            onTextEdited: {
                                indicateError = false
                            }
                        }

                        ImCheckBox {
                            id: chkShowPassword
                            Layout.columnSpan: 2
                            text: qsTr("Show password")
                            checked: true
                        }

                        Text {
                            text: qsTr("Wifi country:")
                            color: parent.enabled ? "black" : "grey"
                        }
                        ComboBox {
                            id: fieldWifiCountry
                            editable: true
                        }
                    }

                    ImCheckBox {
                        id: chkLocale
                        text: qsTr("Set locale settings")
                    }
                    GridLayout {
                        enabled: chkLocale.checked
                        Layout.leftMargin: 40
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: -5

                        Text {
                            text: qsTr("Time zone:")
                            color: parent.enabled ? "black" : "grey"
                        }
                        ComboBox {
                            id: fieldTimezone
                            editable: true
                            Layout.minimumWidth: 200
                        }

                        Text {
                            text: qsTr("Keyboard layout:")
                            color: parent.enabled ? "black" : "grey"
                        }
                        ComboBox {
                            id: fieldKeyboardLayout
                            editable: true
                            Layout.minimumWidth: 200
                        }
                        ImCheckBox {
                            id: chkSkipFirstUse
                            text: qsTr("Skip first-run wizard")
                        }
                    }
                }
            }

            GroupBox {
                title: qsTr("Persistent settings")
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: -10

                    ImCheckBox {
                        id: chkBeep
                        text: qsTr("Play sound when finished")
                    }
                    ImCheckBox {
                        id: chkEject
                        text: qsTr("Eject media when finished")
                    }
                    ImCheckBox {
                        id: chkTelemtry
                        text: qsTr("Enable telemetry")
                    }
                }
            }
          }
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter | Qt.AlignBottom
            Layout.bottomMargin: 10
            spacing: 20

            ImButton {
                text: qsTr("SAVE")
                onClicked: {
                    if (chkSetUser.checked && fieldUserPassword.text.length == 0)
                    {
                        fieldUserPassword.indicateError = true
                        fieldUserPassword.forceActiveFocus()
                        return
                    }
                    if (chkSetUser.checked && fieldUserName.text.length == 0)
                    {
                        fieldUserName.indicateError = true
                        fieldUserName.forceActiveFocus()
                        return
                    }

                    if (chkWifi.checked)
                    {
                        if (fieldWifiPassword.text.length < 8 || fieldWifiPassword.text.length > 64)
                        {
                            fieldWifiPassword.indicateError = true
                            fieldWifiPassword.forceActiveFocus()
                        }
                        if (fieldWifiSSID.text.length == 0)
                        {
                            fieldWifiSSID.indicateError = true
                            fieldWifiSSID.forceActiveFocus()
                        }
                        if (fieldWifiSSID.indicateError || fieldWifiPassword.indicateError)
                        {
                            return
                        }
                    }

                    applySettings()
                    saveSettings()
                    popup.close()
                }
                Material.foreground: activeFocus ? "#d1dcfb" : "#ffffff"
                Material.background: "#c51a4a"
            }

            Text { text: " " }
        }
    }

    function initialize() {
        chkBeep.checked = imageWriter.getBoolSetting("beep")
        chkTelemtry.checked = imageWriter.getBoolSetting("telemetry")
        chkEject.checked = imageWriter.getBoolSetting("eject")
        var settings = imageWriter.getSavedCustomizationSettings()
        fieldTimezone.model = imageWriter.getTimezoneList()
        fieldPublicKey.text = imageWriter.getDefaultPubKey()
        fieldWifiCountry.model = imageWriter.getCountryList()
        fieldKeyboardLayout.model = imageWriter.getKeymapLayoutList()

        if (Object.keys(settings).length) {
            comboSaveSettings.currentIndex = 1
            hasSavedSettings = true
        }
        if ('disableOverscan' in settings) {
            chkOverscan.checked = true
        }
        if ('hostname' in settings) {
            fieldHostname.text = settings.hostname
            chkHostname.checked = true
        }
        if ('sshUserPassword' in settings) {
            fieldUserPassword.text = settings.sshUserPassword
            fieldUserPassword.alreadyCrypted = true
            chkSetUser.checked = true
            /* Older imager versions did not have a sshEnabled setting.
               Assume it is true if it does not exists and sshUserPassword is set */
            if (!('sshEnabled' in settings) || settings.sshEnabled === "true" || settings.sshEnabled === true) {
                chkSSH.checked = true
                radioPasswordAuthentication.checked = true
            }
        }
        if ('sshUserName' in settings) {
            fieldUserName.text = settings.sshUserName
            chkSetUser.checked = true
        }
        if ('sshAuthorizedKeys' in settings) {
            fieldPublicKey.text = settings.sshAuthorizedKeys
            chkSSH.checked = true
            radioPubKeyAuthentication.checked = true
        }
        if ('wifiSSID' in settings) {
            fieldWifiSSID.text = settings.wifiSSID
            if ('wifiSSIDHidden' in settings && settings.wifiSSIDHidden) {
                chkWifiSSIDHidden.checked = true
            }
            chkShowPassword.checked = false
            fieldWifiPassword.text = settings.wifiPassword
            fieldWifiCountry.currentIndex = fieldWifiCountry.find(settings.wifiCountry)
            if (fieldWifiCountry.currentIndex == -1) {
                fieldWifiCountry.editText = settings.wifiCountry
            }
            chkWifi.checked = true
        } else {
            fieldWifiCountry.currentIndex = fieldWifiCountry.find("GB")
            fieldWifiSSID.text = imageWriter.getSSID()
            if (fieldWifiSSID.text.length) {
                fieldWifiPassword.text = imageWriter.getPSK(fieldWifiSSID.text)
                if (fieldWifiPassword.text.length) {
                    chkShowPassword.checked = false
                    if (Qt.platform.os == "osx") {
                        /* User indicated wifi must be prefilled */
                        chkWifi.checked = true
                    }
                }
            }
        }

        var tz;
        if ('timezone' in settings) {
            chkLocale.checked = true
            tz = settings.timezone
        } else {
            tz = imageWriter.getTimezone()
        }
        var tzidx = fieldTimezone.find(tz)
        if (tzidx === -1) {
            fieldTimezone.editText = tz
        } else {
            fieldTimezone.currentIndex = tzidx
        }
        if ('keyboardLayout' in settings) {
            fieldKeyboardLayout.currentIndex = fieldKeyboardLayout.find(settings.keyboardLayout)
            if (fieldKeyboardLayout.currentIndex == -1) {
                fieldKeyboardLayout.editText = settings.keyboardLayout
            }
        } else {
            if (imageWriter.isEmbeddedMode())
            {
                fieldKeyboardLayout.currentIndex = fieldKeyboardLayout.find(imageWriter.getCurrentKeyboard())
            }
            else
            {
                /* Lacking an easy cross-platform to fetch keyboard layout
                   from host system, just default to "gb" for people in
                   UK time zone for now, and "us" for everyone else */
                if (tz == "Europe/London") {
                    fieldKeyboardLayout.currentIndex = fieldKeyboardLayout.find("gb")
                } else {
                    fieldKeyboardLayout.currentIndex = fieldKeyboardLayout.find("us")
                }
            }
        }
        if ('skipFirstUse' in settings) {
            chkSkipFirstUse.checked = true
        }

        initialized = true
    }

    function openPopup() {
        if (!initialized) {
            initialize()
        }

        open()
        popupbody.forceActiveFocus()
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
        return "'"+arg.replace(/'/g, "\\'")+"'"
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

        if (chkOverscan.checked) {
            addConfig("disable_overscan=1")
        }
        if (chkHostname.checked && fieldHostname.length) {
            addFirstRun("CURRENT_HOSTNAME=`cat /etc/hostname | tr -d \" \\t\\n\\r\"`")
            addFirstRun("echo "+fieldHostname.text+" >/etc/hostname")
            addFirstRun("sed -i \"s/127.0.1.1.*$CURRENT_HOSTNAME/127.0.1.1\\t"+fieldHostname.text+"/g\" /etc/hosts")

            addCloudInit("hostname: "+fieldHostname.text)
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

        if (chkSSH.checked || chkSetUser.checked) {
            // First user may not be called 'pi' on all distributions, so look username up
            addFirstRun("FIRSTUSER=`getent passwd 1000 | cut -d: -f1`");
            addFirstRun("FIRSTUSERHOME=`getent passwd 1000 | cut -d: -f6`")

            addCloudInit("users:")
            addCloudInit("- name: "+fieldUserName.text)
            addCloudInit("  groups: users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo")
            addCloudInit("  shell: /bin/bash")

            var cryptedPassword;
            if (chkSetUser.checked) {
                cryptedPassword = fieldUserPassword.alreadyCrypted ? fieldUserPassword.text : imageWriter.crypt(fieldUserPassword.text)
                addCloudInit("  lock_passwd: false")
                addCloudInit("  passwd: "+cryptedPassword)
            }

            if (chkSSH.checked && radioPubKeyAuthentication.checked) {
                var pubkey = fieldPublicKey.text
                var pubkeyArr = pubkey.split("\n")

                if (pubkey.length) {
                    addFirstRun("install -o \"$FIRSTUSER\" -m 700 -d \"$FIRSTUSERHOME/.ssh\"")
                    addFirstRun("install -o \"$FIRSTUSER\" -m 600 <(printf \""+pubkey.replace(/\n/g, "\\n")+"\") \"$FIRSTUSERHOME/.ssh/authorized_keys\"")
                }
                addFirstRun("echo 'PasswordAuthentication no' >>/etc/ssh/sshd_config")

                if (!chkSetUser.checked) {
                    addCloudInit("  lock_passwd: true")
                }
                addCloudInit("  ssh_authorized_keys:")
                for (var i=0; i<pubkeyArr.length; i++) {
                    var pk = pubkeyArr[i].trim();
                    if (pk) {
                        addCloudInit("    - "+pk)
                    }
                }
                addCloudInit("  sudo: ALL=(ALL) NOPASSWD:ALL")
            }
            addCloudInit("")

            if (chkSSH.checked && radioPasswordAuthentication.checked) {
                addCloudInit("ssh_pwauth: true")
            }

            if (chkSetUser.checked) {
                /* Rename first ("pi") user if a different desired username was specified */
                addFirstRun("if [ -f /usr/lib/userconf-pi/userconf ]; then")
                addFirstRun("   /usr/lib/userconf-pi/userconf "+escapeshellarg(fieldUserName.text)+" "+escapeshellarg(cryptedPassword))
                addFirstRun("else")
                addFirstRun("   echo \"$FIRSTUSER:\""+escapeshellarg(cryptedPassword)+" | chpasswd -e")
                addFirstRun("   if [ \"$FIRSTUSER\" != \""+fieldUserName.text+"\" ]; then")
                addFirstRun("      usermod -l \""+fieldUserName.text+"\" \"$FIRSTUSER\"")
                addFirstRun("      usermod -m -d \"/home/"+fieldUserName.text+"\" \""+fieldUserName.text+"\"")
                addFirstRun("      groupmod -n \""+fieldUserName.text+"\" \"$FIRSTUSER\"")
                addFirstRun("      if grep -q \"^autologin-user=\" /etc/lightdm/lightdm.conf ; then")
                addFirstRun("         sed /etc/lightdm/lightdm.conf -i -e \"s/^autologin-user=.*/autologin-user="+fieldUserName.text+"/\"")
                addFirstRun("      fi")
                addFirstRun("      if [ -f /etc/systemd/system/getty@tty1.service.d/autologin.conf ]; then")
                addFirstRun("         sed /etc/systemd/system/getty@tty1.service.d/autologin.conf -i -e \"s/$FIRSTUSER/"+fieldUserName.text+"/\"")
                addFirstRun("      fi")
                addFirstRun("      if [ -f /etc/sudoers.d/010_pi-nopasswd ]; then")
                addFirstRun("         sed -i \"s/^$FIRSTUSER /"+fieldUserName.text+" /\" /etc/sudoers.d/010_pi-nopasswd")
                addFirstRun("      fi")
                addFirstRun("   fi")
                addFirstRun("fi")
            }

            if (chkSSH.checked) {
                addFirstRun("systemctl enable ssh")
            }
            addCloudInit("")
        }
        if (chkWifi.checked) {
            var wpaconfig = "country="+fieldWifiCountry.editText+"\n"
            wpaconfig += "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
            wpaconfig += "ap_scan=1\n\n"
            wpaconfig += "update_config=1\n"
            wpaconfig += "network={\n"
            if (chkWifiSSIDHidden.checked) {
                wpaconfig += "\tscan_ssid=1\n"
            }
            wpaconfig += "\tssid=\""+fieldWifiSSID.text+"\"\n"
            var cryptedPsk = fieldWifiPassword.text.length == 64 ? fieldWifiPassword.text : imageWriter.pbkdf2(fieldWifiPassword.text, fieldWifiSSID.text)
            wpaconfig += "\tpsk="+cryptedPsk+"\n"
            wpaconfig += "}\n"

            addFirstRun("cat >/etc/wpa_supplicant/wpa_supplicant.conf <<'WPAEOF'")
            addFirstRun(wpaconfig)
            addFirstRun("WPAEOF")
            addFirstRun("chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf")
            addFirstRun("rfkill unblock wifi")
            addFirstRun("for filename in /var/lib/systemd/rfkill/*:wlan ; do")
            addFirstRun("  echo 0 > $filename")
            addFirstRun("done")

            cloudinitnetwork  = "version: 2\n"
            cloudinitnetwork += "wifis:\n"
            cloudinitnetwork += "  renderer: networkd\n"
            cloudinitnetwork += "  wlan0:\n"
            cloudinitnetwork += "    dhcp4: true\n"
            cloudinitnetwork += "    optional: true\n"
            cloudinitnetwork += "    access-points:\n"
            cloudinitnetwork += "      \""+fieldWifiSSID.text+"\":\n"
            cloudinitnetwork += "        password: \""+cryptedPsk+"\"\n"
            if (chkWifiSSIDHidden.checked) {
                cloudinitnetwork += "        hidden: true\n"
            }

            /* FIXME: setting wifi country code broken on Ubuntu
               For unknown reasons udev does not trigger setregdomain automatically and as a result
               our setting in /etc/default/crda is being ignored by Ubuntu. */
            addCloudInitRun("sed -i 's/^\s*REGDOMAIN=\S*/REGDOMAIN="+fieldWifiCountry.editText+"/' /etc/default/crda || true")
        }
        if (chkLocale.checked) {
            if (chkSkipFirstUse.checked) {
                addFirstRun("rm -f /etc/xdg/autostart/piwiz.desktop")
                addCloudInitRun("rm -f /etc/xdg/autostart/piwiz.desktop")
            }

            var kbdconfig = "XKBMODEL=\"pc105\"\n"
            kbdconfig += "XKBLAYOUT=\""+fieldKeyboardLayout.editText+"\"\n"
            kbdconfig += "XKBVARIANT=\"\"\n"
            kbdconfig += "XKBOPTIONS=\"\"\n"

            addFirstRun("rm -f /etc/localtime")
            addFirstRun("echo \""+fieldTimezone.editText+"\" >/etc/timezone")
            addFirstRun("dpkg-reconfigure -f noninteractive tzdata")
            addFirstRun("cat >/etc/default/keyboard <<'KBEOF'")
            addFirstRun(kbdconfig)
            addFirstRun("KBEOF")
            addFirstRun("dpkg-reconfigure -f noninteractive keyboard-configuration")

            addCloudInit("timezone: "+fieldTimezone.editText)
            addCloudInitRun("localectl set-x11-keymap \""+fieldKeyboardLayout.editText+"\" pc105")
            addCloudInitRun("setupcon -k --force || true")
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
        if (comboSaveSettings.currentIndex == 1) {
            hasSavedSettings = true
            var settings = { };
            if (chkOverscan.checked) {
                settings.disableOverscan = true
            }
            if (chkHostname.checked && fieldHostname.length) {
                settings.hostname = fieldHostname.text
            }
            if (chkSetUser.checked) {
                settings.sshUserName = fieldUserName.text
                settings.sshUserPassword = fieldUserPassword.alreadyCrypted ? fieldUserPassword.text : imageWriter.crypt(fieldUserPassword.text)
            }

            settings.sshEnabled = chkSSH.checked
            if (chkSSH.checked && radioPubKeyAuthentication.checked) {
                settings.sshAuthorizedKeys = fieldPublicKey.text
            }
            if (chkWifi.checked) {
                settings.wifiSSID = fieldWifiSSID.text
                if (chkWifiSSIDHidden.checked) {
                    settings.wifiSSIDHidden = true
                }
                settings.wifiPassword = fieldWifiPassword.text.length == 64 ? fieldWifiPassword.text : imageWriter.pbkdf2(fieldWifiPassword.text, fieldWifiSSID.text)
                settings.wifiCountry = fieldWifiCountry.editText
            }
            if (chkLocale.checked) {
                settings.timezone = fieldTimezone.editText
                settings.keyboardLayout = fieldKeyboardLayout.editText
                if (chkSkipFirstUse.checked) {
                    settings.skipFirstUse = true
                }
            }

            imageWriter.setSavedCustomizationSettings(settings)

        } else if (hasSavedSettings) {
            imageWriter.clearSavedCustomizationSettings()
            hasSavedSettings = false
        }

        imageWriter.setSetting("beep", chkBeep.checked)
        imageWriter.setSetting("eject", chkEject.checked)
        imageWriter.setSetting("telemetry", chkTelemtry.checked)
    }
}
