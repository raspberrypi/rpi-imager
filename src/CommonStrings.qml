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

    readonly property var repoFiltersList: withAll([
        qsTr("Imager Repository Files (*.json)")
    ])
    readonly property string repoFiltersString: toFilterString(repoFiltersList)

    // --- SSH public key filters ---
    readonly property var sshFiltersList: withAll([
        qsTr("Public Key files (*.pub)")
    ])
    readonly property string sshFiltersString: toFilterString(sshFiltersList)
}
