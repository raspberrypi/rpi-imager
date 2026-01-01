/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * MarqueeText: A Text component that scrolls horizontally on hover when truncated.
 *
 * When the text fits within the available width, it displays normally.
 * When truncated, hovering triggers a smooth left-to-right scroll animation
 * that reveals the full text, then pauses and returns to the start.
 */

import QtQuick
import QtQuick.Layouts

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

    // Layout hints - default to content size
    // Note: Don't set Layout.fillWidth here - let parent decide
    implicitWidth: textMetrics.width
    implicitHeight: textMetrics.height

    // Accessibility
    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
    Accessible.ignored: false

    // Measure the full text width
    TextMetrics {
        id: textMetrics
        text: root.text
    }

    // Clip to bounds so scrolling text doesn't overflow
    clip: true

    // The actual text element that will be animated
    Text {
        id: scrollingText
        text: root.text
        font: textMetrics.font
        color: root.color
        
        // Position for animation
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        
        // Don't elide - we want to show full text when scrolling
        elide: Text.ElideNone
        
        // Accessibility handled by parent
        Accessible.ignored: true
    }

    // Static display when not hovering (with elide)
    Text {
        id: staticText
        text: root.text
        font: textMetrics.font
        color: root.color
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: root.horizontalAlignment
        elide: Text.ElideRight
        visible: !mouseArea.containsMouse || !root.truncated
        
        // Accessibility handled by parent
        Accessible.ignored: true
    }

    // Hide scrolling text when static is visible
    states: [
        State {
            name: "scrolling"
            when: mouseArea.containsMouse && root.truncated
            PropertyChanges { target: scrollingText; visible: true }
            PropertyChanges { target: staticText; visible: false }
        },
        State {
            name: "static"
            when: !mouseArea.containsMouse || !root.truncated
            PropertyChanges { target: scrollingText; visible: false; x: 0 }
            PropertyChanges { target: staticText; visible: true }
        }
    ]

    // Scroll animation sequence
    SequentialAnimation {
        id: scrollAnimation
        running: false
        loops: Animation.Infinite

        // Pause at start
        PauseAnimation { duration: root.pauseAtStartMs }

        // Scroll left to reveal hidden text (with padding to ensure full visibility)
        NumberAnimation {
            target: scrollingText
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
            target: scrollingText
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

        onContainsMouseChanged: {
            if (containsMouse && root.truncated) {
                scrollingText.x = 0
                scrollAnimation.restart()
            } else {
                scrollAnimation.stop()
                scrollingText.x = 0
            }
        }
    }
}

