/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma Singleton

import QtQuick

Item {
    // Commonly reused translatable strings
    readonly property string warningRiskText: qsTr("Selecting the wrong drive will permanently erase data and can render your computer inoperable.")
    readonly property string warningProceedText: qsTr("Only proceed if you fully understand the risks.")
    readonly property string systemDriveText: qsTr("System drives typically contain files essential to the operation of your computer, and may include your personal files (photos, videos, documents).")

    // --- New reusable filters ---
    readonly property string allFilesLabel: qsTr("All files (*)")
    readonly property var    allFilesList: [ allFilesLabel ]
    readonly property string allFilesString: allFilesLabel

    function withAll(list)          { return list.concat([allFilesLabel]) }
    function toFilterString(list)   { return list.join(";;") }

    readonly property var imageExtensions: ["*.img","*.zip","*.iso","*.gz","*.xz","*.zst","*.wic"]

    readonly property var imageFiltersList: withAll([
        qsTr("Image files (%1)").arg(imageExtensions.join(" "))
    ])
    readonly property string imageFiltersString: toFilterString(imageFiltersList)

    // Repository file extensions - must match MANIFEST_EXTENSION in src/config.h
    readonly property string manifestExtension: "rpi-imager-manifest"
    readonly property var repoFiltersList: withAll([
        qsTr("Imager Repository Files (*.json *.%1)").arg(manifestExtension)
    ])
    readonly property string repoFiltersString: toFilterString(repoFiltersList)

    // --- SSH public key filters ---
    readonly property var sshFiltersList: withAll([
        qsTr("Public Key files (*.pub)"),
        qsTr("Authorized keys files (authorized_keys)")
    ])
    readonly property string sshFiltersString: toFilterString(sshFiltersList)

    // --- Common UI strings ---
    readonly property string cancel: qsTr("Cancel")
    readonly property string yes: qsTr("Yes")
    readonly property string no: qsTr("No")
    readonly property string browse: qsTr("Browse")
    readonly property string continueText: qsTr("Continue")
    readonly property string back: qsTr("Back")
    readonly property string finish: qsTr("Finish")
    readonly property string selectImage: qsTr("Select image")
    readonly property string password: qsTr("Password:")
    readonly property string device: qsTr("Device:")
    readonly property string storage: qsTr("Storage:")
    
    // --- "No X selected" patterns ---
    readonly property string noDeviceSelected: qsTr("No device selected")
    readonly property string noImageSelected: qsTr("No image selected")
    readonly property string noStorageSelected: qsTr("No storage selected")
    
    // --- Configuration status messages (without bullets) ---
    readonly property string hostnameConfigured: qsTr("Hostname configured")
    readonly property string userAccountConfigured: qsTr("User account configured")
    readonly property string sshEnabled: qsTr("SSH enabled")
    readonly property string localeConfigured: qsTr("Localisation configured")
    readonly property string wifiConfigured: qsTr("Wi‑Fi configured")
    readonly property string piConnectEnabled: qsTr("Raspberry Pi Connect enabled")
    readonly property string usbGadgetEnabled: qsTr("USB Gadget mode enabled")
    readonly property string i2cEnabled: qsTr("I2C enabled")
    readonly property string spiEnabled: qsTr("SPI enabled")
    readonly property string onewireEnabled: qsTr("1-Wire enabled")
    readonly property string serialConfigured: qsTr("Serial configured")
}
