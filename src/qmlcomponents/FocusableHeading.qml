/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * FocusableHeading: a FocusableText with the Heading accessibility role, for
 * titles and section headers. See FocusableText for the screen-reader focus
 * behaviour.
 */

pragma ComponentBehavior: Bound

import QtQuick

import RpiImager

FocusableText {
    Accessible.role: Accessible.Heading
}
