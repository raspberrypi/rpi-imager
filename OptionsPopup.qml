/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi (Trading) Limited
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2

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
                    }
                }

                ColumnLayout {
                    spacing: -10

                    CheckBox {
                        id: chkOverscan
                        text: qsTr("Disable overscan")
                    }
                    RowLayout {
                        CheckBox {
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
                    CheckBox {
                        id: chkSSH
                        text: qsTr("Enable SSH")
                        onCheckedChanged: {
                            if (checked) {
                                if (!radioPasswordAuthentication.checked && !radioPubKeyAuthentication.checked) {
                                    radioPasswordAuthentication.checked = true
                                }
                                if (radioPasswordAuthentication.checked && !fieldUserPassword.length) {
                                    fieldUserPassword.forceActiveFocus()
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        enabled: chkSSH.checked
                        Layout.leftMargin: 40
                        spacing: -5

                        RadioButton {
                            id: radioPasswordAuthentication
                            text: qsTr("Use password authentication")
                            onCheckedChanged: {
                                if (checked) {
                                    fieldUserPassword.forceActiveFocus()
                                }
                            }
                        }

                        GridLayout {
                            Layout.leftMargin: 40
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: -5
                            enabled: radioPasswordAuthentication.checked

                            Text {
                                text: qsTr("Set password for 'pi' user:")
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

                        RadioButton {
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
                                text: qsTr("Set authorized_keys for 'pi':")
                                color: parent.enabled ? "black" : "grey"
                            }
                            TextField {
                                id: fieldPublicKey
                                Layout.minimumWidth: 200
                            }
                        }
                    }

                    CheckBox {
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

                        CheckBox {
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

                    CheckBox {
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
                        TextField {
                            id: fieldKeyboardLayout
                            Layout.minimumWidth: 200
                            text: "us"
                        }
                        CheckBox {
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

                    CheckBox {
                        id: chkBeep
                        text: qsTr("Play sound when finished")
                    }
                    CheckBox {
                        id: chkEject
                        text: qsTr("Eject media when finished")
                    }
                    CheckBox {
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

            Button {
                text: qsTr("SAVE")
                onClicked: {
                    if (chkSSH.checked && radioPasswordAuthentication.checked && fieldUserPassword.text.length == 0)
                    {
                        fieldUserPassword.indicateError = true
                        fieldUserPassword.forceActiveFocus()
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
                Material.foreground: "#ffffff"
                Material.background: "#c51a4a"
                font.family: roboto.name
                Accessible.onPressAction: clicked()
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
            chkSSH.checked = true
            radioPasswordAuthentication.checked = true
        }
        if ('sshAuthorizedKeys' in settings) {
            fieldPublicKey.text = settings.sshAuthorizedKeys
            chkSSH.checked = true
            radioPubKeyAuthentication.checked = true
        }
        if ('wifiSSID' in settings) {
            fieldWifiSSID.text = settings.wifiSSID
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
            fieldKeyboardLayout.text = settings.keyboardLayout
        } else {
            /* Lacking an easy cross-platform to fetch keyboard layout
               from host system, just default to "gb" for people in
               UK time zone for now, and "us" for everyone else */
            if (tz == "Europe/London") {
                fieldKeyboardLayout.text = "gb"
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

    function applySettings()
    {
        cmdline = ""
        config = ""
        firstrun = ""

        if (chkOverscan.checked) {
            addConfig("disable_overscan=1")
        }
        if (chkHostname.checked && fieldHostname.length) {
            addFirstRun("CURRENT_HOSTNAME=`cat /etc/hostname | tr -d \" \\t\\n\\r\"`")
            addFirstRun("echo "+fieldHostname.text+" >/etc/hostname")
            addFirstRun("sed -i \"s/127.0.1.1.*$CURRENT_HOSTNAME/127.0.1.1\\t"+fieldHostname.text+"/g\" /etc/hosts")
        }
        if (chkSSH.checked) {
            // First user may not be called 'pi' on all distributions, so look username up
            addFirstRun("FIRSTUSER=`getent passwd 1000 | cut -d: -f1`");
            addFirstRun("FIRSTUSERHOME=`getent passwd 1000 | cut -d: -f6`")

            if (radioPasswordAuthentication.checked) {
                var cryptedPassword = fieldUserPassword.alreadyCrypted ? fieldUserPassword.text : imageWriter.crypt(fieldUserPassword.text)
                addFirstRun("echo \"$FIRSTUSER:\""+escapeshellarg(cryptedPassword)+" | chpasswd -e")
            }
            if (radioPubKeyAuthentication.checked) {
                var pubkey = fieldPublicKey.text.replace(/\n/g, "")
                if (pubkey.length) {
                    addFirstRun("install -o \"$FIRSTUSER\" -m 700 -d \"$FIRSTUSERHOME/.ssh\"")
                    addFirstRun("install -o \"$FIRSTUSER\" -m 600 <(echo \""+pubkey+"\") \"$FIRSTUSERHOME/.ssh/authorized_keys\"")
                }
                addFirstRun("echo 'PasswordAuthentication no' >>/etc/ssh/sshd_config")
            }
            addFirstRun("systemctl enable ssh")
        }
        if (chkWifi.checked) {
            var wpaconfig = "country="+fieldWifiCountry.editText+"\n"
            wpaconfig += "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
            wpaconfig += "ap_scan=1\n\n"
            wpaconfig += "update_config=1\n"
            wpaconfig += "network={\n"
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
        }
        if (chkLocale.checked) {
            if (chkSkipFirstUse) {
                addFirstRun("rm -f /etc/xdg/autostart/piwiz.desktop")
            }

            addFirstRun("rm -f /etc/localtime")
            addFirstRun("echo \""+fieldTimezone.editText+"\" >/etc/timezone")
            addFirstRun("dpkg-reconfigure -f noninteractive tzdata")
            addFirstRun("cat >/etc/default/keyboard <<'KBEOF'")
            addFirstRun("XKBMODEL=\"pc105\"")
            addFirstRun("XKBLAYOUT=\""+fieldKeyboardLayout.text+"\"")
            addFirstRun("XKBVARIANT=\"\"")
            addFirstRun("XKBOPTIONS=\"\"")
            addFirstRun("KBEOF")
            addFirstRun("dpkg-reconfigure -f noninteractive keyboard-configuration")
        }

        if (firstrun.length) {
            firstrun = "#!/bin/bash\n\n"+"set +e\n\n"+firstrun
            addFirstRun("rm -f /boot/firstrun.sh")
            addFirstRun("sed -i 's| systemd.run.*||g' /boot/cmdline.txt")
            addFirstRun("exit 0")
            /* using systemd.run_success_action=none does not seem to have desired effect
               systemd then stays at "reached target kernel command line", so use reboot instead */
            addCmdline("systemd.run=/boot/firstrun.sh systemd.run_success_action=reboot systemd.unit=kernel-command-line.target")
        }

        imageWriter.setImageCustomization(config, cmdline, firstrun)
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
            if (chkSSH.checked) {
                if (radioPasswordAuthentication.checked) {
                    settings.sshUserPassword = fieldUserPassword.alreadyCrypted ? fieldUserPassword.text : imageWriter.crypt(fieldUserPassword.text)
                }
                if (radioPubKeyAuthentication.checked) {
                    settings.sshAuthorizedKeys = fieldPublicKey.text
                }
            }
            if (chkWifi.checked) {
                settings.wifiSSID = fieldWifiSSID.text
                settings.wifiPassword = fieldWifiPassword.text
                settings.wifiCountry = fieldWifiCountry.editText
            }
            if (chkLocale.checked) {
                settings.timezone = fieldTimezone.editText
                settings.keyboardLayout = fieldKeyboardLayout.text
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
