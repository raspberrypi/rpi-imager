/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Material 2.2

Button {
    font.family: roboto.name
    font.capitalization: Font.AllUppercase
    Material.background: activeFocus ? "#d1dcfb" : "#ffffff"
    Material.foreground: "#cd2355"
    Material.roundedScale: Material.ExtraSmallScale
    Accessible.onPressAction: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onReturnPressed: clicked()
}
