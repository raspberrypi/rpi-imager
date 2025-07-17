/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

pragma Singleton

import QtQuick 2.15

Item {
    id: root

    // === COLORS ===
    readonly property color mainBackgroundColor: "#cd2355"

    readonly property color buttonBackgroundColor: "#ffffff"
    readonly property color buttonForegroundColor: mainBackgroundColor
    readonly property color buttonFocusedBackgroundColor: "#d1dcfb"
    readonly property color buttonHoveredBackgroundColor: "#f2f2f2"

    readonly property color button2BackgroundColor: mainBackgroundColor
    readonly property color button2ForegroundColor: "#ffffff"
    readonly property color button2FocusedBackgroundColor: "#32a0d7"
    readonly property color button2HoveredBackgroundColor: "#b71f4d"

    readonly property color titleBackgroundColor: "#f5f5f5"
    readonly property color titleSeparatorColor: "#afafaf"
    readonly property color popupBorderColor: "#dcdcdc"

    readonly property color listViewRowBackgroundColor: "#ffffff"
    readonly property color listViewHoverRowBackgroundColor: titleBackgroundColor
    readonly property color listViewHighlightColor: "lightsteelblue"

    // descriptions in list views
    readonly property color textDescriptionColor: "#1a1a1a"

    // OS metadata
    readonly property color textMetadataColor: "#646464"

    // for the "device / OS / storage" titles
    readonly property color subtitleColor: "#ffffff"

    readonly property color progressBarTextColor: "white"
    readonly property color progressBarVerifyForegroundColor: "#6cc04a"
    readonly property color progressBarBackgroundColor: "#d15d7d"

    readonly property color lanbarBackgroundColor: "#ffffe3"

    /// the check-boxes/radio-buttons have labels that might be disabled
    readonly property color formLabelColor: "black"
    readonly property color formLabelErrorColor: "red"
    readonly property color formLabelDisabledColor: "grey"

    readonly property color embeddedModeInfoTextColor: "#ffffff"

    // === FONTS ===
    readonly property alias fontFamily: roboto.name
    readonly property alias fontFamilyLight: robotoLight.name
    readonly property alias fontFamilyBold: robotoBold.name

    // Font sizes
    readonly property int fontSizeTitle: 24
    readonly property int fontSizeSubtitle: 14
    readonly property int fontSizeFormLabel: 14
    readonly property int fontSizeInput: 12
    readonly property int fontSizeDescription: 12

    // === SPACING ===
    readonly property int spacingSmall: 10
    readonly property int spacingMedium: 15
    readonly property int spacingLarge: 20
    readonly property int spacingExtraLarge: 30

    // === SIZES ===
    readonly property int buttonHeightStandard: 40
    readonly property int buttonWidthMinimum: 120
    readonly property int buttonWidthSkip: 150
    
    readonly property int sectionMaxWidth: 500
    readonly property int sectionMargins: 30
    readonly property int sectionPadding: 20
    readonly property int sectionBorderWidth: 1
    readonly property int sectionBorderRadius: 8

    // === LAYOUT ===
    readonly property int formColumnSpacing: 20
    readonly property int formRowSpacing: 15
    readonly property int stepContentMargins: 30
    readonly property int stepContentSpacing: 20

    // Font loaders
    FontLoader { id: roboto;      source: "fonts/Roboto-Regular.ttf" }
    FontLoader { id: robotoLight; source: "fonts/Roboto-Light.ttf" }
    FontLoader { id: robotoBold;  source: "fonts/Roboto-Bold.ttf" }
}
