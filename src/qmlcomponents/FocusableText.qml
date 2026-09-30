/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * FocusableText: a Text that joins the keyboard Tab order only while a screen
 * reader is active, so assistive tech can reach it without slowing down sighted
 * keyboard users. All bindings track ImageWriterSingleton.screenReaderActive live,
 * so toggling a screen reader at runtime updates focusability immediately.
 *
 * Defaults to the StaticText role with the element's text as its accessible name;
 * override Accessible.role / Accessible.name per instance where needed. For
 * headings use FocusableHeading.
 */

pragma ComponentBehavior: Bound

import QtQuick

import RpiImager

Text {
    // Plain unless a caller says otherwise. AutoText reads markup in a string
    // as rich text and fetches what an <img> in it names, and several of these
    // quote back a repository's words, a device's firmware, or a settings file
    // written by hand. The few that really are markup set StyledText here.
    textFormat: Text.PlainText

    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Accessible.focusable: ImageWriterSingleton.screenReaderActive
    focusPolicy: ImageWriterSingleton.screenReaderActive ? Qt.TabFocus : Qt.NoFocus
    activeFocusOnTab: ImageWriterSingleton.screenReaderActive
}
