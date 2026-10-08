/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import RpiImager

/**
 * A QR code for `text`, drawn from the modules ImageWriter computes with the
 * vendored qrcodegen. Dark modules on white with the four-module quiet zone
 * scanners expect, whatever the theme around it.
 */
Item {
    id: root

    property string text: ""
    // Modules a side, without the quiet zone; 0 when there is nothing to draw.
    readonly property int moduleCount: _code.size || 0

    readonly property var _code: (ImageWriterSingleton && root.text.length > 0)
                                 ? ImageWriterSingleton.qrCode(root.text) : ({})
    readonly property int _quiet: 4

    implicitWidth: 200
    implicitHeight: 200
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("QR code")

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "white"
            ctx.fillRect(0, 0, width, height)
            var n = root.moduleCount
            if (n <= 0)
                return
            var total = n + 2 * root._quiet
            // Whole pixels per module, so no module blurs into its neighbour.
            var cell = Math.max(1, Math.floor(Math.min(width, height) / total))
            var origin = Math.floor((Math.min(width, height) - cell * total) / 2) + cell * root._quiet
            var modules = root._code.modules
            ctx.fillStyle = "black"
            for (var y = 0; y < n; ++y)
                for (var x = 0; x < n; ++x)
                    if (modules.charAt(y * n + x) === "1")
                        ctx.fillRect(origin + x * cell, origin + y * cell, cell, cell)
        }
    }

    onTextChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
