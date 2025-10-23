/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import "../qmlcomponents"

import RpiImager

FocusScope {
    id: root
    
    property string title: ""
    property string subtitle: ""
    property bool showBackButton: true
    property bool showNextButton: true
    property bool showSkipButton: false
    property string nextButtonText: qsTr("Next")
    property string backButtonText: CommonStrings.back
    property string skipButtonText: qsTr("Skip customisation")
    property bool nextButtonEnabled: true
    property bool backButtonEnabled: true
    property bool skipButtonEnabled: true
    property alias content: contentArea.children
    // Step may set the first item to receive focus when the step becomes visible
    property var initialFocusItem: null
    // Expose action buttons for KeyNavigation in child content
    property alias nextButtonItem: nextButton
    property alias backButtonItem: backButton
    property alias skipButtonItem: skipButton
    // Reference to the App Options button from the parent WizardContainer
    property var appOptionsButton: null
    onAppOptionsButtonChanged: {
        // Rebuild navigation when App Options button is connected
        if (appOptionsButton) {
            Qt.callLater(rebuildFocusOrder)
        }
    }

    // Focus groups: array of { name, order, getItemsFn, enabled }
    property var _focusGroups: []
    // Flattened focus items after composition
    property var _focusableItems: []
    
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
            visible: (root.title && root.title.length > 0) || (root.subtitle && root.subtitle.length > 0)
            
            Text {
                id: titleText
                text: root.title
                font.pixelSize: Style.fontSizeTitle
                font.family: Style.fontFamilyBold
                font.bold: true
                color: Style.formLabelColor
                Layout.fillWidth: true
                visible: root.title && root.title.length > 0
                Accessible.role: Accessible.Heading
                Accessible.name: root.title
                Accessible.ignored: false
            }
            
            Text {
                id: subtitleText
                text: root.subtitle
                font.pixelSize: Style.fontSizeSubtitle
                font.family: Style.fontFamily
                color: Style.textDescriptionColor
                Layout.fillWidth: true
                visible: root.subtitle && root.subtitle.length > 0
                Accessible.role: Accessible.StaticText
                Accessible.name: root.subtitle
                Accessible.ignored: false
            }
        }
        
        // Content area (no Flickable to prevent input interception)
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
                // Tab order among action buttons: next -> back -> skip -> wrap to first field
                KeyNavigation.tab: root.initialFocusItem ? root.initialFocusItem : nextButton
                KeyNavigation.backtab: nextButton
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
                // After back, Tab goes to skip; Shift+Tab goes to next
                KeyNavigation.tab: skipButton
                KeyNavigation.backtab: nextButton
            }
            
            ImButtonRed {
                id: nextButton
                text: root.nextButtonText
                visible: root.showNextButton
                enabled: root.nextButtonEnabled
                Layout.minimumWidth: Style.buttonWidthMinimum
                Layout.preferredHeight: Style.buttonHeightStandard
                onClicked: root.nextClicked()
                // After next, Tab goes to back; Shift+Tab goes to skip
                KeyNavigation.tab: backButton
                KeyNavigation.backtab: skipButton
            }
        }
    }

    Component.onCompleted: {
        rebuildFocusOrder()
        if (initialFocusItem && typeof initialFocusItem.forceActiveFocus === 'function') {
            initialFocusItem.forceActiveFocus()
        }
    }

    onVisibleChanged: {
        if (visible && initialFocusItem && typeof initialFocusItem.forceActiveFocus === 'function') {
            initialFocusItem.forceActiveFocus()
        }
    }

    // Public API for steps
    function registerFocusGroup(name, getItemsFn, order) {
        if (order === undefined) order = 0
        // Replace if exists
        for (var i = 0; i < _focusGroups.length; i++) {
            if (_focusGroups[i].name === name) {
                _focusGroups[i] = { name: name, getItemsFn: getItemsFn, order: order, enabled: true }
                rebuildFocusOrder()
                return
            }
        }
        _focusGroups.push({ name: name, getItemsFn: getItemsFn, order: order, enabled: true })
        rebuildFocusOrder()
    }

    function setFocusGroupEnabled(name, enabled) {
        for (var i = 0; i < _focusGroups.length; i++) {
            if (_focusGroups[i].name === name) {
                _focusGroups[i].enabled = enabled
                rebuildFocusOrder()
                return
            }
        }
    }

    // requestRecomputeTabOrder was used when keyboard tab navigation order was dynamic.
    // With simplified navigation and no tabbing requirement, this is no longer needed.

    function rebuildFocusOrder() {
        // Compose enabled groups by order
        _focusGroups.sort(function(a,b){ return a.order - b.order })
        var items = []
        for (var i = 0; i < _focusGroups.length; i++) {
            var g = _focusGroups[i]
            if (!g.enabled || !g.getItemsFn) continue
            var arr = g.getItemsFn()
            if (!arr || !arr.length) continue
            for (var k = 0; k < arr.length; k++) {
                var it = arr[k]
                if (it && it.visible && it.enabled && typeof it.forceActiveFocus === 'function') {
                    items.push(it)
                }
            }
        }
        _focusableItems = items

        // Build complete navigation chain in order: Next -> Back -> Skip -> App Options -> Fields
        var navigationChain = []
        
        // Add navigation buttons in order (include if visible, regardless of enabled state)
        if (nextButton.visible) navigationChain.push(nextButton)
        if (backButton.visible) navigationChain.push(backButton)
        if (skipButton.visible) navigationChain.push(skipButton)
        if (appOptionsButton && appOptionsButton.visible) navigationChain.push(appOptionsButton)
        
        // Add all focusable fields
        for (var i = 0; i < _focusableItems.length; i++) {
            navigationChain.push(_focusableItems[i])
        }
        
        // Wire the complete chain with perfect circular navigation
        for (var j = 0; j < navigationChain.length; j++) {
            var current = navigationChain[j]
            var nextInChain = (j + 1 < navigationChain.length) ? navigationChain[j + 1] : navigationChain[0]
            var prevInChain = (j > 0) ? navigationChain[j - 1] : navigationChain[navigationChain.length - 1]
            
            if (current && current.KeyNavigation) {
                current.KeyNavigation.tab = nextInChain
                current.KeyNavigation.backtab = prevInChain
            } else {
                // Only log exceptional cases where KeyNavigation is missing
                console.log("WizardStepBase: WARNING - Failed to wire KeyNavigation for item:", current)
            }
        }

        // Ensure initialFocusItem set
        var firstField = _focusableItems.length > 0 ? _focusableItems[0] : null
        if (!initialFocusItem) initialFocusItem = firstField
    }

    // Helper functions for global Tab navigation fallback
    function getNextFocusableElement(startElement) {
        if (_focusableItems.length === 0) {
            return nextButton.visible && nextButton.enabled ? nextButton : null
        }

        var allFocusable = _focusableItems.slice() // copy array
        if (nextButton.visible && nextButton.enabled) allFocusable.push(nextButton)
        if (backButton.visible && backButton.enabled) allFocusable.push(backButton)
        if (skipButton.visible && skipButton.enabled) allFocusable.push(skipButton)

        if (allFocusable.length === 0) {
            return null
        }

        var currentIndex = allFocusable.indexOf(startElement)
        if (currentIndex === -1) {
            return allFocusable[0]
        }

        var nextIndex = (currentIndex + 1) % allFocusable.length
        return allFocusable[nextIndex]
    }

    function getPreviousFocusableElement(startElement) {
        if (_focusableItems.length === 0) {
            return nextButton.visible && nextButton.enabled ? nextButton : null
        }

        var allFocusable = _focusableItems.slice() // copy array
        if (nextButton.visible && nextButton.enabled) allFocusable.push(nextButton)
        if (backButton.visible && backButton.enabled) allFocusable.push(backButton)
        if (skipButton.visible && skipButton.enabled) allFocusable.push(skipButton)

        if (allFocusable.length === 0) {
            return null
        }

        var currentIndex = allFocusable.indexOf(startElement)
        if (currentIndex === -1) {
            return allFocusable[allFocusable.length - 1]
        }
        
        var prevIndex = (currentIndex - 1 + allFocusable.length) % allFocusable.length
        return allFocusable[prevIndex]
    }
} 
