/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

Item {
    id: root
    
    property string title: ""
    property string subtitle: ""
    property bool showBackButton: true
    property bool showNextButton: true
    property bool showSkipButton: false
    property string nextButtonText: qsTr("Next")
    property string backButtonText: qsTr("Back")
    property string skipButtonText: qsTr("Skip customisation")
    property bool nextButtonEnabled: true
    property bool backButtonEnabled: true
    property bool skipButtonEnabled: true
    property alias content: contentArea.children
    
    signal nextClicked()
    signal backClicked()
    signal skipClicked()
    
    // Main layout
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Style.stepContentMargins
        anchors.rightMargin: Style.stepContentMargins
        anchors.topMargin: Style.stepContentMargins
        anchors.bottomMargin: Style.spacingSmall
        spacing: Style.stepContentSpacing
        
        // Header section
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.spacingSmall
            
            Text {
                id: titleText
                text: root.title
                font.pixelSize: Style.fontSizeTitle
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.subtitleColor
                Layout.fillWidth: true
            }
            
            Text {
                id: subtitleText
                text: root.subtitle
                font.pixelSize: Style.fontSizeSubtitle
                font.family: Style.fontFamily
                color: Style.subtitleColor
                Layout.fillWidth: true
                visible: root.subtitle.length > 0
            }
        }
        
        // Content area
        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        
        // Navigation buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.spacingMedium
            
            ImButton {
                id: skipButton
                text: root.skipButtonText
                visible: root.showSkipButton
                enabled: root.skipButtonEnabled
                Layout.minimumWidth: Style.buttonWidthSkip
                Layout.preferredHeight: Style.buttonHeightStandard
                onClicked: root.skipClicked()
            }
            
            Item {
                Layout.fillWidth: true
            }
            
            ImButton {
                id: backButton
                text: root.backButtonText
                visible: root.showBackButton
                enabled: root.backButtonEnabled
                Layout.minimumWidth: Style.buttonWidthMinimum
                Layout.preferredHeight: Style.buttonHeightStandard
                onClicked: root.backClicked()
            }
            
            ImButton {
                id: nextButton
                text: root.nextButtonText
                visible: root.showNextButton
                enabled: root.nextButtonEnabled
                Layout.minimumWidth: Style.buttonWidthMinimum
                Layout.preferredHeight: Style.buttonHeightStandard
                onClicked: root.nextClicked()
            }
        }
    }
} 