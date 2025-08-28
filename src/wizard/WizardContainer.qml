/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */
pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

import RpiImager

Item {
    id: root
    
    required property ImageWriter imageWriter
    property var optionsPopup: null
    // Reference to the full-window overlay root for dialog parenting
    property var overlayRootRef: null
    // Expose network info text for embedded mode status updates
    property alias networkInfoText: networkInfo.text
    
    property int currentStep: 0
    readonly property int totalSteps: 10  // Internal steps (unchanged)
    
    // Track writing state
    property bool isWriting: false
    
    // Track selections for display in summary
    property string selectedDeviceName: ""
    property string selectedOsName: ""
    property string selectedStorageName: ""
    
    // Track customizations that were actually configured
    property bool hostnameConfigured: false
    property bool localeConfigured: false
    property bool userConfigured: false
    property bool wifiConfigured: false
    property bool sshEnabled: false
    property bool piConnectEnabled: false
    // Ephemeral per-run setting: do not persist across runs
    property bool disableWarnings: false
    // Whether the selected OS supports customisation (init_format present)
    property bool customizationSupported: true
    
    // Wizard steps enum
    readonly property int stepDeviceSelection: 0
    readonly property int stepOSSelection: 1
    readonly property int stepStorageSelection: 2
    readonly property int stepHostnameCustomization: 3
    readonly property int stepLocaleCustomization: 4
    readonly property int stepUserCustomization: 5
    readonly property int stepWifiCustomization: 6
    readonly property int stepRemoteAccess: 7
    readonly property int stepWriting: 8
    readonly property int stepDone: 9
    
    signal wizardCompleted()
    
    Component.onCompleted: {
        // Default to disabling warnings in embedded mode (per-run, non-persistent)
        if (imageWriter && imageWriter.isEmbeddedMode()) {
            disableWarnings = true
        }
    }

    // Wizard step names for sidebar (grouped for cleaner display)
    readonly property var stepNames: [
        qsTr("Device"),
        qsTr("OS"), 
        qsTr("Storage"),
        qsTr("Customization"),
        qsTr("Writing"),
        qsTr("Done")
    ]
    
    // Helper function to map wizard step to sidebar index
    function getSidebarIndex(wizardStep) {
        if (wizardStep <= stepStorageSelection) {
            return wizardStep
        } else if (wizardStep >= stepHostnameCustomization && wizardStep <= stepRemoteAccess) {
            return 3 // Customization group
        } else if (wizardStep === stepWriting) {
            return 4 // Writing
        } else if (wizardStep === stepDone) {
            return 5 // Done
        }
        return 0
    }

    // Map sidebar index back to the first wizard step in that group
    function getWizardStepFromSidebarIndex(sidebarIndex) {
        switch (sidebarIndex) {
            case 0: return stepDeviceSelection
            case 1: return stepOSSelection
            case 2: return stepStorageSelection
            case 3: return stepHostnameCustomization // first customization step
            case 4: return stepWriting
            case 5: return stepDone
            default: return stepDeviceSelection
        }
    }
    
    // Main horizontal layout
    RowLayout {
        anchors.fill: parent
        spacing: 0
        
        // Sidebar
        Rectangle {
            Layout.preferredWidth: Style.sidebarWidth
            Layout.fillHeight: true
            color: Style.sidebarBackgroundColour
            border.color: Style.sidebarBorderColour
            border.width: 0
            
            Flickable {
                id: sidebarScroll
                clip: true
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: sidebarBottom.top
                anchors.margins: Style.cardPadding
                contentWidth: -1
                contentHeight: sidebarColumn.implicitHeight
                z: 1

                ColumnLayout {
                    id: sidebarColumn
                    width: parent.width
                    spacing: Style.spacingXSmall
                
                // Header
                Text {
                    id: sidebarHeader
                    text: qsTr("Setup Steps")
                    font.pixelSize: Style.fontSizeHeading
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: Style.sidebarTextOnInactiveColor
                    Layout.fillWidth: true
                    Layout.bottomMargin: Style.spacingSmall
                }
                
                // Step list
                Repeater {
                    model: root.stepNames
                    
                    Rectangle {
                        id: stepItem
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: (function(){
                            var base = Style.sidebarItemHeight
                            return sublistContainer.visible ? (base + Style.spacingXXSmall + sublistContainer.implicitHeight) : base
                        })()
                        color: Style.transparent
                        border.color: Style.transparent
                        border.width: 0
                        radius: 0
                        property bool isClickable: stepItem.index < root.getSidebarIndex(root.currentStep) && !root.isWriting
 
                        // Header band with active background/border
                        Rectangle {
                            id: headerRect
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: Style.sidebarItemHeight
                            color: stepItem.index === root.getSidebarIndex(root.currentStep) ? Style.sidebarActiveBackgroundColor : Style.transparent
                            border.color: stepItem.index === root.getSidebarIndex(root.currentStep) ? Style.sidebarActiveBackgroundColor : Style.transparent
                            border.width: 1
                            radius: Style.sidebarItemBorderRadius

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: stepItem.isClickable
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    var targetStep = root.getWizardStepFromSidebarIndex(stepItem.index)
                                    if (root.currentStep > targetStep && !root.isWriting) {
                                        root.jumpToStep(targetStep)
                                    }
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Style.spacingSmall
                                spacing: Style.spacingTiny
                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: stepItem.modelData
                                    font.pixelSize: Style.fontSizeSidebarItem
                                    font.family: Style.fontFamily
                                    color: (stepItem.index > root.getSidebarIndex(root.currentStep) || (stepItem.index === 3 && !root.customizationSupported))
                                               ? Style.formLabelDisabledColor
                                               : (stepItem.index === root.getSidebarIndex(root.currentStep)
                                                   ? Style.sidebarTextOnActiveColor
                                                   : Style.sidebarTextOnInactiveColor)
                                    elide: Text.ElideRight
                                }
                            }
                        }
 
                        // Inline customization sub-steps under the 'Customization' item
                        Column {
                            id: sublistContainer
                            anchors.top: headerRect.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: Style.spacingXXSmall
                            x: Style.spacingExtraLarge
                            width: parent.width - Style.spacingExtraLarge
                            spacing: Style.spacingXXSmall
                            visible: stepItem.index === 3 && root.currentStep >= root.stepHostnameCustomization && root.currentStep <= root.stepRemoteAccess
 
                            Repeater {
                                model: [qsTr("Hostname"), qsTr("Locale"), qsTr("User"), qsTr("WiFi"), qsTr("Remote Access")]
                                Rectangle {
                                    id: subItem
                                    required property int index
                                    required property var modelData
                                    width: parent.width
                                    height: Style.sidebarSubItemHeight
                                    radius: Style.sidebarItemBorderRadius
                                    color: (root.currentStep - root.stepHostnameCustomization) === subItem.index ? Style.sidebarActiveBackgroundColor : Style.transparent
                                    border.color: (root.currentStep - root.stepHostnameCustomization) === subItem.index ? Style.sidebarActiveBackgroundColor : Style.transparent
                                    border.width: 1
 
                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: !root.isWriting && (root.stepHostnameCustomization + subItem.index) <= root.currentStep
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            var target = root.stepHostnameCustomization + subItem.index
                                            if (root.currentStep !== target) root.jumpToStep(target)
                                        }
                                    }
                                    RowLayout {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins: Style.spacingSmall
                                        anchors.leftMargin: Style.spacingMedium
                                        Text {
                                            Layout.fillWidth: true
                                            Layout.alignment: Qt.AlignVCenter
                                            text: subItem.modelData
                                            font.pixelSize: Style.fontSizeCaption
                                            font.family: Style.fontFamily
                                            color: ((root.stepHostnameCustomization + subItem.index) > root.currentStep)
                                                       ? Style.formLabelDisabledColor
                                                       : (((root.currentStep - root.stepHostnameCustomization) === subItem.index)
                                                           ? Style.sidebarTextOnActiveColor
                                                           : Style.sidebarTextOnInactiveColor)
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Spacer
                Item {
                    Layout.fillHeight: true
                }
                // Embedded mode network info (hidden by default) with marquee scrolling
                Item {
                    id: networkInfoContainer
                    Layout.fillWidth: true
                    visible: root.imageWriter.isEmbeddedMode() && networkInfo.text.length > 0
                    Layout.preferredHeight: networkInfo.implicitHeight
                    clip: true

                    // Scrolling row that contains two copies of the text for seamless loop
                    RowLayout {
                        id: marqueeRow
                        anchors.fill: parent
                        anchors.margins: 0
                        spacing: Style.spacingMedium
                        // Use x animation below; disable layout's x management
                        Layout.fillWidth: false

                        Text {
                            id: networkInfo
                            Layout.alignment: Qt.AlignVCenter
                            font.pixelSize: Style.fontSizeCaption
                            font.family: Style.fontFamily
                            color: Style.textDescriptionColor
                            text: ""
                        }
                        Text {
                            id: networkInfoCopy
                            Layout.alignment: Qt.AlignVCenter
                            font.pixelSize: Style.fontSizeCaption
                            font.family: Style.fontFamily
                            color: Style.textDescriptionColor
                            text: networkInfo.text
                        }
                    }

                    PropertyAnimation {
                        id: marqueeAnim
                        target: marqueeRow
                        property: "x"
                        from: 0
                        to: -(networkInfo.width + marqueeRow.spacing)
                        duration: Math.max(4000, Math.round((networkInfo.width + marqueeRow.spacing + networkInfoContainer.width) / 40 * 1000))
                        loops: Animation.Infinite
                        running: networkInfoContainer.visible && networkInfo.text.length > 0
                    }

                    onVisibleChanged: {
                        if (visible) marqueeAnim.restart(); else marqueeAnim.stop()
                    }
                    Component.onCompleted: marqueeAnim.start()
                }
                
                // [moved] Advanced options lives outside the scroll area
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
            // Fixed bottom container for Advanced Options
            Item {
                id: sidebarBottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Style.cardPadding
                height: optionsButtonRect.height
                z: 2

                Rectangle {
                    id: optionsButtonRect
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Style.buttonHeightStandard - 5
                    color: optionsButton.containsMouse ? Style.translucentWhite10 : Style.mainBackgroundColor
                    border.color: Style.sidebarControlBorderColor
                    border.width: 1
                    radius: Style.sidebarItemBorderRadius
                    
                    MouseArea {
                        id: optionsButton
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.optionsPopup) {
                                if (!root.optionsPopup.wizardContainer) {
                                    root.optionsPopup.wizardContainer = root
                                }
                                root.optionsPopup.initialize()
                                root.optionsPopup.open()
                            }
                        }
                    }
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Style.spacingTiny
                        spacing: Style.spacingTiny
                        
                        Image {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            source: "../icons/ic_cog_red.svg"
                            sourceSize.width: 16
                            sourceSize.height: 16
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Advanced Options")
                            font.pixelSize: Style.fontSizeCaption
                            font.family: Style.fontFamily
                            color: Style.sidebarTextOnInactiveColor
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        // Vertical separator between sidebar and content
        Item {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: parent.height * 0.75
                color: Style.titleSeparatorColor
            }
        }

        // Main content area
        StackView {
            id: wizardStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            initialItem: deviceSelectionStep
            
            // Smooth transitions between steps
            pushEnter: Transition {
                PropertyAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 250
                }
            }
            
            pushExit: Transition {
                PropertyAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: 250
                }
            }
            
            popEnter: Transition {
                PropertyAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 250
                }
            }
            
            popExit: Transition {
                PropertyAnimation {
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: 250
                }
            }
        }
    }
    
    // Navigation functions
    function nextStep() {
        if (root.currentStep < root.totalSteps - 1) {
            var nextIndex = root.currentStep + 1
            // If customization is not supported, skip customization steps entirely
            if (!customizationSupported && nextIndex === stepHostnameCustomization) {
                nextIndex = stepWriting
            }
            root.currentStep = nextIndex
            var nextComponent = getStepComponent(root.currentStep)
            if (nextComponent) {
                // Destroy previous step to avoid lingering handlers, then show the next step
                wizardStack.clear()
                wizardStack.push(nextComponent)
            }
        }
    }
    
    function previousStep() {
        if (root.currentStep > 0) {
            var prevIndex = root.currentStep - 1
            root.currentStep = prevIndex
            var prevComponent = getStepComponent(root.currentStep)
            if (prevComponent) {
                // Clear and push the previous step explicitly since we keep a single-item stack
                wizardStack.clear()
                wizardStack.push(prevComponent)
            }
        }
    }
    
    function jumpToStep(stepIndex) {
        if (stepIndex >= 0 && stepIndex < root.totalSteps) {
            root.currentStep = stepIndex
            var stepComponent = getStepComponent(stepIndex)
            if (stepComponent) {
                // Clear the stack and push the target step
                wizardStack.clear()
                wizardStack.push(stepComponent)
            }
        }
    }
    
    function getStepComponent(stepIndex) {
        switch(stepIndex) {
            case stepDeviceSelection: return deviceSelectionStep
            case stepOSSelection: return osSelectionStep
            case stepStorageSelection: return storageSelectionStep
            case stepHostnameCustomization: return hostnameCustomizationStep
            case stepLocaleCustomization: return localeCustomizationStep
            case stepUserCustomization: return userCustomizationStep
            case stepWifiCustomization: return wifiCustomizationStep
            case stepRemoteAccess: return remoteAccessStep
            case stepWriting: return writingStep
            case stepDone: return doneStep
            default: return null
        }
    }
    
    // Step components
    Component {
        id: deviceSelectionStep
        DeviceSelectionStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            showBackButton: false
            onNextClicked: root.nextStep()
        }
    }
    
    Component {
        id: osSelectionStep
        OSSelectionStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
        }
    }
    
    Component {
        id: storageSelectionStep
        StorageSelectionStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
        }
    }
    
    Component {
        id: hostnameCustomizationStep
        HostnameCustomizationStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
            onSkipClicked: {
                // Skip functionality is handled in the step itself
            }
        }
    }
    
    Component {
        id: localeCustomizationStep
        LocaleCustomizationStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
            onSkipClicked: {
                // Skip functionality is handled in the step itself
            }
        }
    }
    
    Component {
        id: userCustomizationStep
        UserCustomizationStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
            onSkipClicked: {
                // Skip functionality is handled in the step itself
            }
        }
    }
    
    Component {
        id: wifiCustomizationStep
        WifiCustomizationStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
            onSkipClicked: {
                // Skip functionality is handled in the step itself
            }
        }
    }
    
    Component {
        id: remoteAccessStep
        RemoteAccessStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
            onSkipClicked: {
                // Skip functionality is handled in the step itself
            }
        }
    }
    
    Component {
        id: writingStep
        WritingStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            showBackButton: true
            // Let WritingStep handle its own button text based on state
            onNextClicked: {
                // Only advance if the step indicates it's ready
                if (isComplete) {
                    root.nextStep()
                }
                // Otherwise, let the step handle the action internally
            }
            onBackClicked: root.previousStep()
        }
    }
    
    Component {
        id: doneStep
        DoneStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            showBackButton: false
            nextButtonText: qsTr("Finish")
            onNextClicked: root.wizardCompleted()
        }
    }
    
    function onFinalizing() {
        // Forward to the WritingStep if currently active
        if (currentStep === stepWriting && wizardStack.currentItem) {
            wizardStack.currentItem.onFinalizing()
        }
    }
    
    function onDownloadProgress(now, total) {
        // Forward to the WritingStep if currently active
        if (currentStep === stepWriting && wizardStack.currentItem) {
            wizardStack.currentItem.onDownloadProgress(now, total)
        }
    }
    
    function onVerifyProgress(now, total) {
        // Forward to the WritingStep if currently active
        if (currentStep === stepWriting && wizardStack.currentItem) {
            wizardStack.currentItem.onVerifyProgress(now, total)
        }
    }
    
    function onPreparationStatusUpdate(msg) {
        // Forward to the WritingStep if currently active
        if (currentStep === stepWriting && wizardStack.currentItem) {
            wizardStack.currentItem.onPreparationStatusUpdate(msg)
        }
    }
    
    function resetWizard() {
        // Reset all wizard state to initial values
        currentStep = 0
        isWriting = false
        selectedDeviceName = ""
        selectedOsName = ""
        selectedStorageName = ""
        hostnameConfigured = false
        localeConfigured = false
        userConfigured = false
        wifiConfigured = false
        sshEnabled = false
        piConnectEnabled = false
        
        // Navigate back to the first step
        wizardStack.clear()
        wizardStack.push(deviceSelectionStep)
    }

    // Keep customization items visible when navigating within customization
    onCurrentStepChanged: {
        if (!sidebarScroll) return
        if (currentStep >= stepHostnameCustomization && currentStep <= stepRemoteAccess) {
            var idx = currentStep - stepHostnameCustomization
            var mainRowH = Style.sidebarItemHeight + Style.spacingXSmall
            var subRectH = Style.sidebarSubItemHeight
            var subRowH = subRectH + Style.spacingXXSmall
            var baseY = sidebarHeader.y + sidebarHeader.implicitHeight + Style.spacingSmall + mainRowH * (3 + 1) + Style.spacingXXSmall
            var target = baseY + idx * subRowH - (sidebarScroll.height/2 - subRectH/2)
            if (target < 0) target = 0
            var maxY = sidebarScroll.contentHeight - sidebarScroll.height
            if (target > maxY) target = Math.max(0, maxY)
            sidebarScroll.contentY = target
        } else {
            // Center main group item
            var sidebarIdx = getSidebarIndex(currentStep)
            var mainRowH = Style.sidebarItemHeight + Style.spacingXSmall
            // account for header and its bottom margin
            var target2 = sidebarHeader.y + sidebarHeader.implicitHeight + Style.spacingSmall + sidebarIdx * mainRowH - (sidebarScroll.height/2 - Style.sidebarItemHeight/2)
            if (target2 < 0) target2 = 0
            var maxY2 = sidebarScroll.contentHeight - sidebarScroll.height
            if (target2 > maxY2) target2 = Math.max(0, maxY2)
            sidebarScroll.contentY = target2
        }
    }
} 