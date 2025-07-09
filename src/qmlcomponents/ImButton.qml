/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2

import RpiImager

Button {
    font.family: Style.fontFamily
    font.capitalization: Font.AllUppercase
    Material.background: activeFocus ? Style.buttonFocusedBackgroundColor : Style.buttonBackgroundColor
    Material.foreground: Style.buttonForegroundColor
    Material.roundedScale: Material.ExtraSmallScale
    activeFocusOnTab: true
    Accessible.onPressAction: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onReturnPressed: clicked()
}
