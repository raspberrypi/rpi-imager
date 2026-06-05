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
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Accessible.focusable: ImageWriterSingleton.screenReaderActive
    focusPolicy: ImageWriterSingleton.screenReaderActive ? Qt.TabFocus : Qt.NoFocus
    activeFocusOnTab: ImageWriterSingleton.screenReaderActive
}
