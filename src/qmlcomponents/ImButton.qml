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
    Material.background: activeFocus ? "#d1dcfb" : "#ffffff"
    Material.foreground: "#cd2355"
    Material.roundedScale: Material.ExtraSmallScale
    Accessible.onPressAction: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onReturnPressed: clicked()
}
