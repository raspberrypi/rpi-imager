/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * MarqueeText: A Text component that scrolls horizontally on hover when truncated.
 *
 * When the text fits within the available width, it displays normally (elided).
 * When truncated, hovering triggers a smooth left-to-right scroll animation
 * that reveals the full text, then pauses and returns to the start.
 *
 * A single rendered Text drives both the static (elided) and scrolling display;
 * the headless TextMetrics provides the natural width used to detect truncation
 * and size the scroll, independent of the rendered element's elide state.
 */

pragma ComponentBehavior: Bound

import QtQuick

import RpiImager

Item {
    id: root

    // Text properties (mirror common Text properties)
    property string text: ""
    property alias font: textMetrics.font
    property color color: Style.formLabelColor
    property int horizontalAlignment: Text.AlignLeft

    // Animation tuning
    property int scrollSpeed: 40         // pixels per second
    property int pauseAtEndMs: 1500      // pause at the end before resetting
    property int pauseAtStartMs: 500     // pause before starting scroll
    property int scrollPadding: 8        // extra pixels to scroll to ensure full text is visible

    // Expose whether text is truncated (for external tooltip logic)
    readonly property bool truncated: textMetrics.width > root.width

    // True while actively scrolling: hovered, truncated and motion allowed
    readonly property bool scrolling: mouseArea.containsMouse && root.truncated && !PlatformHelper.prefersReducedMotion

    // Layout hints - default to content size
    // Note: Don't set Layout.fillWidth here - let parent decide
    implicitWidth: textMetrics.width
    implicitHeight: textMetrics.height

    // Accessibility
    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
    Accessible.ignored: false

    // Clip to bounds so scrolling text doesn't overflow
    clip: true

    // Measure the full (unelided) text width, independent of how it is rendered
    TextMetrics {
        id: textMetrics
        text: root.text
    }

    // Single rendered element: elides when static, shows full text and animates x when scrolling
    Text {
        id: label
        text: root.text
        font: textMetrics.font
        color: root.color
        width: root.width
        anchors.verticalCenter: parent.verticalCenter
        // Left-align while scrolling so the reveal starts from the beginning
        horizontalAlignment: root.scrolling ? Text.AlignLeft : root.horizontalAlignment
        elide: root.scrolling ? Text.ElideNone : Text.ElideRight
        x: 0

        // Accessibility handled by parent
        Accessible.ignored: true
    }

    // Scroll animation sequence, driven declaratively by the scrolling state
    SequentialAnimation {
        id: scrollAnimation
        running: root.scrolling
        loops: Animation.Infinite

        // Reset to the start whenever scrolling stops
        onRunningChanged: if (!running) label.x = 0

        // Pause at start
        PauseAnimation { duration: root.pauseAtStartMs }

        // Scroll left to reveal hidden text (with padding to ensure full visibility)
        NumberAnimation {
            target: label
            property: "x"
            from: 0
            to: -(textMetrics.width - root.width + root.scrollPadding)
            duration: Math.max(100, (textMetrics.width - root.width + root.scrollPadding) / root.scrollSpeed * 1000)
            easing.type: Easing.Linear
        }

        // Pause at end
        PauseAnimation { duration: root.pauseAtEndMs }

        // Quick reset to start
        NumberAnimation {
            target: label
            property: "x"
            to: 0
            duration: 300
            easing.type: Easing.OutQuad
        }
    }

    // Mouse area for hover detection
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton  // Don't consume clicks
    }
}
