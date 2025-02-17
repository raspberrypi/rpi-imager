/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls

import RpiImager

ListView {
    id: root
    clip: true

    anchors.left: parent.left
    anchors.bottom: parent.bottom
    boundsBehavior: Flickable.StopAtBounds
    highlight: Rectangle { color: Style.listViewHighlightColor; radius: 5 }

    ScrollBar.vertical: ScrollBar {
        width: 10
        policy: root.contentHeight > root.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    }

    Keys.onEnterPressed: (event) => { Keys.spacePressed(event) }
    Keys.onReturnPressed: (event) => { Keys.spacePressed(event) }
}
