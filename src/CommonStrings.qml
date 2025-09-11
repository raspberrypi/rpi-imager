/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma Singleton

import QtQuick 2.15

Item {
    // Commonly reused translatable strings
    readonly property string warningRiskText: qsTr("Selecting the wrong drive will permanently erase data and can render your computer inoperable.")
    readonly property string warningProceedText: qsTr("Only proceed if you fully understand the risks.")
    readonly property string systemDriveText: qsTr("System drives typically contain files essential to the operation of your computer, and may include your personal files (photos, videos, documents).")

    // --- New reusable filters ---
    readonly property var imageExtensions: ["*.img","*.zip","*.iso","*.gz","*.xz","*.zst","*.wic"]

    readonly property var imageFiltersList: [
        qsTr("Image files (%1)").arg(imageExtensions.join(" ")),
        qsTr("All files (*)")
    ]
    readonly property string imageFiltersString: imageFiltersList.join(";;")

    readonly property var repoFiltersList: [
        qsTr("Imager Repository Files (*.json)"),
        qsTr("All files (*)")
    ]
    readonly property string repoFiltersString: repoFiltersList.join(";;")

    // --- SSH public key filters ---
    readonly property var sshFiltersList: [
        qsTr("Public Key files (*.pub)"),
        qsTr("All files (*)")
    ]
    readonly property string sshFiltersString: sshFiltersList.join(";;")
}
