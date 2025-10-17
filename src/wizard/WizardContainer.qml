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
    // Show landing language selection step at startup
    property bool showLanguageSelection: false
    // Reference to the full-window overlay root for dialog parenting
    property var overlayRootRef: null
    // Expose network info text for embedded mode status updates
    property alias networkInfoText: networkInfo.text
    
    property int currentStep: 0
    readonly property int totalSteps: 12
    
    // Track which steps have been made permissible/unlocked for navigation
    // Each bit represents a step: bit 0 = Device, bit 1 = OS, etc.
    property int permissibleStepsBitmap: 1  // Start with Device step (bit 0) always permissible
    
    // Track writing state
    property bool isWriting: false
    
    // Track selections for display in summary
    property string selectedDeviceName: ""
    property string selectedOsName: ""
    property string selectedStorageName: ""
    
    // Track previous selections to detect changes
    property string previousDeviceName: ""
    property string previousOsName: ""

    property bool supportsSerialConsoleOnly: false
    property bool supportsUsbGadget: false
    
    // Track customizations that were actually configured
    property bool hostnameConfigured: false
    property bool localeConfigured: false
    property bool userConfigured: false
    property bool wifiConfigured: false
    property bool sshEnabled: false
    property bool piConnectEnabled: false
    // Whether selected OS supports Raspberry Raspberry Pi Connect customization
    property bool piConnectAvailable: false

    // Interfaces & Features
    property bool ccRpiAvailable: false
    property bool ifI2cEnabled: false
    property bool ifSpiEnabled: false
    property bool if1WireEnabled: false
    // "Disabled" | "Default" | "Console & Hardware" | "Console" | "Hardware" | ""
    property string ifSerial: ""
    property bool featUsbGadgetEnabled: false

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
    readonly property int stepPiConnectCustomization: 8
    readonly property int stepIfAndFeatures: 9
    readonly property int stepWriting: 10
    readonly property int stepDone: 11
    
    signal wizardCompleted()
    
    // Focus anchor for global keyboard navigation
    Item {
        id: focusAnchor
        focus: true
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Tab) {
                var currentStep = wizardStack.currentItem
                if (currentStep && typeof currentStep.getNextFocusableElement === 'function') {
                    if (event.modifiers & Qt.ShiftModifier) {
                        var prevElement = currentStep.getPreviousFocusableElement(null)
                        if (prevElement && typeof prevElement.forceActiveFocus === 'function') {
                            prevElement.forceActiveFocus()
                        }
                    } else {
                        var nextElement = currentStep.getNextFocusableElement(null)
                        if (nextElement && typeof nextElement.forceActiveFocus === 'function') {
                            nextElement.forceActiveFocus()
                        }
                    }
                    event.accepted = true
                }
            }
        }
    }

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
        qsTr("Customisation"),
        qsTr("Writing"),
        qsTr("Done")
    ]
    
    readonly property int firstCustomizationStep: stepHostnameCustomization

    // Helper function to map wizard step to sidebar index
    function getSidebarIndex(wizardStep) {
        if (wizardStep <= stepStorageSelection) {
            return wizardStep
        } else if (wizardStep >= firstCustomizationStep && wizardStep <= getLastCustomizationStep()) {
            return 3 // Customization group
        } else if (wizardStep === stepWriting) {
            return 4 // Writing
        } else if (wizardStep === stepDone) {
            return 5 // Done
        }
        return 0
    }

    function getLastCustomizationStep() {
        return ccRpiAvailable
            ? stepIfAndFeatures
            : piConnectAvailable
                ? stepPiConnectCustomization
                : stepRemoteAccess
    }

    function getCustomizationSubstepLabels() {
        // Only return labels if customization is supported
        if (!customizationSupported) {
            return []
        }
        
        var labels = [qsTr("Hostname"), qsTr("Localisation"), qsTr("User"), qsTr("Wi‑Fi"), qsTr("Remote access")]
        if (piConnectAvailable) {
            labels.push(qsTr("Raspberry Pi Connect"))
        }
        if (ccRpiAvailable) {
            labels.push(qsTr("Interfaces & Features"))
        }

        return labels
    }

    function isCustomizationSubstepConfigured(subIndex) {
        // Map the display index to the actual step based on what's available
        var labels = getCustomizationSubstepLabels()
        if (subIndex >= labels.length) return false
        
        var stepLabel = labels[subIndex]
        if (stepLabel === qsTr("Hostname")) return hostnameConfigured
        if (stepLabel === qsTr("Localisation")) return localeConfigured
        if (stepLabel === qsTr("User")) return userConfigured
        if (stepLabel === qsTr("Wi‑Fi")) return wifiConfigured
        if (stepLabel === qsTr("Remote access")) return sshEnabled
        if (stepLabel === qsTr("Raspberry Pi Connect")) return piConnectEnabled
        if (stepLabel === qsTr("Interfaces & Features")) return (ifI2cEnabled || ifSpiEnabled || if1WireEnabled || ifSerial !== "" || featUsbGadgetEnabled)
        
        return false
    }

    // Helper functions for managing permissible steps bitmap
    function markStepPermissible(stepIndex) {
        var bit = 1 << stepIndex
        permissibleStepsBitmap |= bit
    }
    
    function isStepPermissible(stepIndex) {
        var bit = 1 << stepIndex
        return (permissibleStepsBitmap & bit) !== 0
    }
    
    function invalidateStepsFrom(fromStepIndex) {
        // Clear all bits from the specified step onwards
        var mask = (1 << fromStepIndex) - 1  // Keep only bits before fromStepIndex
        permissibleStepsBitmap &= mask
    }
    
    function invalidateDeviceDependentSteps() {
        // When device changes, invalidate all steps after device selection
        invalidateStepsFrom(stepOSSelection)
        
        // Clear device-dependent state
        selectedOsName = ""
        selectedStorageName = ""
        customizationSupported = true  // Reset to default
        
        // Clear all customization flags
        hostnameConfigured = false
        localeConfigured = false
        userConfigured = false
        wifiConfigured = false
        sshEnabled = false
        piConnectEnabled = false
        piConnectAvailable = false
        ccRpiAvailable = false
        ifI2cEnabled = false
        ifSpiEnabled = false
        if1WireEnabled = false
        ifSerial = ""
        featUsbGadgetEnabled = false
    }
    
    function invalidateOSDependentSteps() {
        // When OS changes, invalidate storage and later steps
        invalidateStepsFrom(stepStorageSelection)
        
        // Clear OS-dependent state
        selectedStorageName = ""
        
        // Clear customization flags since they depend on the specific OS
        // The OS selection logic will set customizationSupported appropriately
        // and clear these again if needed, but we clear them proactively here
        hostnameConfigured = false
        localeConfigured = false
        userConfigured = false
        wifiConfigured = false
        sshEnabled = false
        piConnectEnabled = false
        
        // Reset OS capability flags - these will be set correctly by OS selection
        piConnectAvailable = false
        ccRpiAvailable = false
        ifI2cEnabled = false
        ifSpiEnabled = false
        if1WireEnabled = false
        ifSerial = ""
        featUsbGadgetEnabled = false
    }


    // Map sidebar index back to the first wizard step in that group
    function getWizardStepFromSidebarIndex(sidebarIndex) {
        switch (sidebarIndex) {
            case 0: return stepDeviceSelection
            case 1: return stepOSSelection
            case 2: return stepStorageSelection
            case 3: return firstCustomizationStep
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
                    // Add right margin when scrollbar is visible to prevent overlap
                    anchors.rightMargin: (sidebarScroll.contentHeight > sidebarScroll.height ? Style.scrollBarWidth : 0)
                
                // Header
                Text {
                    id: sidebarHeader
                    text: qsTr("Setup steps")
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
                        property bool isClickable: (function(){
                            if (root.isWriting) return false
                            // If customization not supported, do not allow navigating back to customization group
                            if (!root.customizationSupported && stepItem.index === 3) return false
                            
                            // Get the step index for this sidebar item
                            var targetStep = root.getWizardStepFromSidebarIndex(stepItem.index)
                            
                            // Allow navigation to any permissible step or backward navigation
                            return root.isStepPermissible(targetStep) || targetStep < root.currentStep
                        })()
 
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
                                    // Guard: skip customization group when unsupported
                                    if (!root.customizationSupported && stepItem.index === 3) {
                                        return
                                    }
                                    // Allow navigation to any permissible step or backward navigation
                                    if (!root.isWriting && (root.isStepPermissible(targetStep) || root.currentStep > targetStep)) {
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
                            visible: stepItem.index === 3 && root.customizationSupported && root.currentStep > root.stepOSSelection

                            Repeater {
                                model: root.getCustomizationSubstepLabels()
                                Rectangle {
                                    id: subItem
                                    required property int index
                                    required property var modelData
                                    width: parent.width
                                    height: Style.sidebarSubItemHeight
                                    radius: Style.sidebarItemBorderRadius
                                    color: Style.transparent
                                    border.color: Style.transparent
                                    border.width: 0

                                    property bool isCurrentStep: {
                                        if (root.currentStep < root.firstCustomizationStep || root.currentStep > root.getLastCustomizationStep()) {
                                            return false
                                        }
                                        
                                        // Map current step to display index by finding which label matches
                                        var labels = root.getCustomizationSubstepLabels()
                                        if (subItem.index >= labels.length) return false
                                        
                                        var currentStepLabel = ""
                                        if (root.currentStep === root.stepHostnameCustomization) currentStepLabel = qsTr("Hostname")
                                        else if (root.currentStep === root.stepLocaleCustomization) currentStepLabel = qsTr("Localisation")
                                        else if (root.currentStep === root.stepUserCustomization) currentStepLabel = qsTr("User")
                                        else if (root.currentStep === root.stepWifiCustomization) currentStepLabel = qsTr("Wi‑Fi")
                                        else if (root.currentStep === root.stepRemoteAccess) currentStepLabel = qsTr("Remote access")
                                        else if (root.currentStep === root.stepPiConnectCustomization) currentStepLabel = qsTr("Raspberry Pi Connect")
                                        else if (root.currentStep === root.stepIfAndFeatures) currentStepLabel = qsTr("Interfaces & Features")
                                        
                                        return labels[subItem.index] === currentStepLabel
                                    }
                                    property bool isConfigured: root.isCustomizationSubstepConfigured(subItem.index)
                                    property bool isClickable: root.customizationSupported && !root.isWriting && root.currentStep > root.stepOSSelection && (
                                        // Allow navigation to any substep if we've reached customization
                                        root.currentStep >= root.firstCustomizationStep ||
                                        // Or if we've been to customization before (any substep configured)
                                        root.hostnameConfigured || root.localeConfigured || root.userConfigured || 
                                        root.wifiConfigured || root.sshEnabled || root.piConnectEnabled ||
                                        // Or if any customization step has been made permissible
                                        root.isStepPermissible(root.stepHostnameCustomization) ||
                                        root.isStepPermissible(root.stepLocaleCustomization) ||
                                        root.isStepPermissible(root.stepUserCustomization) ||
                                        root.isStepPermissible(root.stepWifiCustomization) ||
                                        root.isStepPermissible(root.stepRemoteAccess) ||
                                        root.isStepPermissible(root.stepPiConnectCustomization) ||
                                        root.isStepPermissible(root.stepIfAndFeatures)
                                    )

                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: subItem.isClickable
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            // Map display index to actual step index based on available labels
                                            var labels = root.getCustomizationSubstepLabels()
                                            if (subItem.index >= labels.length) return
                                            
                                            var stepLabel = labels[subItem.index]
                                            var target = root.firstCustomizationStep // default to hostname
                                            
                                            if (stepLabel === qsTr("Hostname")) target = root.stepHostnameCustomization
                                            else if (stepLabel === qsTr("Localisation")) target = root.stepLocaleCustomization
                                            else if (stepLabel === qsTr("User")) target = root.stepUserCustomization
                                            else if (stepLabel === qsTr("Wi‑Fi")) target = root.stepWifiCustomization
                                            else if (stepLabel === qsTr("Remote access")) target = root.stepRemoteAccess
                                            else if (stepLabel === qsTr("Raspberry Pi Connect")) target = root.stepPiConnectCustomization
                                            else if (stepLabel === qsTr("Interfaces & Features")) target = root.stepIfAndFeatures
                                            
                                            // Allow navigation to permissible steps or backward navigation within customization
                                            if (root.currentStep !== target && (root.isStepPermissible(target) || target < root.currentStep)) {
                                                root.jumpToStep(target)
                                            }
                                        }
                                    }
                                    RowLayout {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins: Style.spacingSmall
                                        anchors.leftMargin: Style.spacingMedium
                                        Text {
                                            id: subLabel
                                            Layout.fillWidth: true
                                            Layout.alignment: Qt.AlignVCenter
                                            text: subItem.modelData
                                            font.pixelSize: Style.fontSizeCaption
                                            font.family: Style.fontFamily
                                            font.bold: subItem.isConfigured
                                            font.underline: subItem.isCurrentStep
                                            color: (!root.customizationSupported || !subItem.isClickable)
                                                       ? Style.formLabelDisabledColor
                                                       : Style.sidebarTextOnInactiveColor
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
                        to: -(((networkInfo ? networkInfo.width : 0) + marqueeRow.spacing))
                        duration: Math.max(4000, Math.round((((networkInfo ? networkInfo.width : 0) + marqueeRow.spacing + (networkInfoContainer ? networkInfoContainer.width : 0)) / 40) * 1000))
                        loops: Animation.Infinite
                        running: networkInfoContainer.visible && networkInfo && networkInfo.text && networkInfo.text.length > 0
                    }

                    onVisibleChanged: {
                        if (visible) marqueeAnim.restart(); else marqueeAnim.stop()
                    }
                    Component.onCompleted: marqueeAnim.start()
                }
                
                // [moved] Advanced options lives outside the scroll area
                }
                ScrollBar.vertical: ScrollBar { 
                    width: Style.scrollBarWidth
                    policy: ScrollBar.AsNeeded 
                }
            }
            // Fixed bottom container for Advanced Options
            Item {
                id: sidebarBottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Style.cardPadding
                anchors.rightMargin: Style.cardPadding
                anchors.bottomMargin: Style.spacingSmall
                height: Style.buttonHeightStandard
                z: 2

                ImButton {
                    id: optionsButton
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Style.buttonHeightStandard
                    text: qsTr("App Options")
                    accessibleDescription: qsTr("Open application settings to configure sound alerts, auto-eject, telemetry, and content repository")
                    activeFocusOnTab: true
                    onClicked: {
                        if (root.optionsPopup) {
                            if (!root.optionsPopup.wizardContainer) {
                                root.optionsPopup.wizardContainer = root
                            }
                            // TODO: actually duplicate
                            // as onOpen in it already calls initialize()
                            root.optionsPopup.initialize()
                            root.optionsPopup.open()
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
            
            initialItem: root.showLanguageSelection ? languageSelectionStep : deviceSelectionStep
            
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
            if (!customizationSupported && nextIndex === firstCustomizationStep) {
                nextIndex = stepWriting
            }
            // Skip optional Raspberry Pi Connect step when OS does not support it
            if (!piConnectAvailable && nextIndex === stepPiConnectCustomization) {
                nextIndex++
            }
            // skip interfaces and features for Operating Systems that don't support the cc_raspberry_pi cloud-init module
            if (!ccRpiAvailable && nextIndex == stepIfAndFeatures) {
                nextIndex++
            }
            // Before entering the writing step, persist and apply customization (when supported)
            if (nextIndex === stepWriting && customizationSupported && imageWriter) {
                // Persist whatever is currently staged in per-step UIs
                var settings = imageWriter.getSavedCustomizationSettings()
                imageWriter.setSavedCustomizationSettings(settings)
                // Build and stage customization directly in C++
                imageWriter.applyCustomizationFromSavedSettings()
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
            // From Writing step:
            // - If customization not supported, jump straight back to Storage Selection
            // - If Raspberry Pi Connect step is not available, skip it when navigating back
            if (root.currentStep === stepWriting && !customizationSupported) {
                prevIndex = stepStorageSelection
            } else {
                // skip interfaces and features for Operating Systems that don't support the cc_raspberry_pi cloud-init module
                if (prevIndex == stepIfAndFeatures && !ccRpiAvailable) {
                    prevIndex--
                }
                if (prevIndex === stepPiConnectCustomization && !piConnectAvailable) {
                    prevIndex--
                }
            }
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
            case stepPiConnectCustomization: return piConnectCustomizationStep
            case stepIfAndFeatures: return ifAndFeaturesStep
            case stepWriting: return writingStep
            case stepDone: return doneStep
            default: return null
        }
    }
    
    // Step components
    Component {
        id: languageSelectionStep
        LanguageSelectionStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            appOptionsButton: optionsButton
            onNextClicked: {
                // After choosing language, jump to first real wizard step
                root.jumpToStep(root.stepDeviceSelection)
            }
        }
    }
    Component {
        id: deviceSelectionStep
        DeviceSelectionStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            showBackButton: false
            appOptionsButton: optionsButton
            onNextClicked: root.nextStep()
        }
    }
    
    Component {
        id: osSelectionStep
        OSSelectionStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            appOptionsButton: optionsButton
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
        }
    }
    
    Component {
        id: storageSelectionStep
        StorageSelectionStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            appOptionsButton: optionsButton
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
        }
    }
    
    Component {
        id: hostnameCustomizationStep
        HostnameCustomizationStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            appOptionsButton: optionsButton
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
            appOptionsButton: optionsButton
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
            appOptionsButton: optionsButton
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
            appOptionsButton: optionsButton
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
            appOptionsButton: optionsButton
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
            onSkipClicked: {
                // Skip functionality is handled in the step itself
            }
        }
    }

    Component {
        id: piConnectCustomizationStep
        PiConnectCustomizationStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            appOptionsButton: optionsButton
            onNextClicked: root.nextStep()
            onBackClicked: root.previousStep()
            onSkipClicked: {
                // Skip functionality is handled in the step itself
            }
        }
    }

    Component {
        id: ifAndFeaturesStep
        IfAndFeaturesCustomizationStep {
            imageWriter: root.imageWriter
            wizardContainer: root
            appOptionsButton: optionsButton
            onNextClicked: {
                // Only advance if the step indicates it's ready
                if (isConfirmed) {
                    root.nextStep()
                }
                // Otherwise, let the step handle the action internally
            }
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
            appOptionsButton: optionsButton
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
            nextButtonText: CommonStrings.finish
            appOptionsButton: optionsButton
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
        permissibleStepsBitmap = 1  // Reset to only Device step permissible
        isWriting = false
        selectedDeviceName = ""
        selectedOsName = ""
        selectedStorageName = ""
        previousDeviceName = ""
        previousOsName = ""
        hostnameConfigured = false
        localeConfigured = false
        userConfigured = false
        wifiConfigured = false
        sshEnabled = false
        piConnectEnabled = false
        piConnectAvailable = false

        ccRpiAvailable = false
        ifI2cEnabled = false
        ifSpiEnabled = false
        if1WireEnabled = false
        ifSerial = ""
        featUsbGadgetEnabled = false

        supportsSerialConsoleOnly = false
        supportsUsbGadget = false
        
        // Navigate back to the first step
        wizardStack.clear()
        wizardStack.push(deviceSelectionStep)
    }

    // Detect device selection changes and invalidate dependent steps
    onSelectedDeviceNameChanged: {
        if (previousDeviceName !== "" && previousDeviceName !== selectedDeviceName) {
            console.log("Device changed from", previousDeviceName, "to", selectedDeviceName, "- invalidating dependent steps")
            invalidateDeviceDependentSteps()
        }
        previousDeviceName = selectedDeviceName
    }
    
    // Detect OS selection changes and invalidate dependent steps
    onSelectedOsNameChanged: {
        if (previousOsName !== "" && previousOsName !== selectedOsName) {
            console.log("OS changed from", previousOsName, "to", selectedOsName, "- invalidating dependent steps")
            invalidateOSDependentSteps()
        }
        previousOsName = selectedOsName
    }

    // Keep customization items visible when navigating within customization
    onCurrentStepChanged: {
        // Mark the current step as permissible for future navigation
        markStepPermissible(currentStep)
        
        if (!sidebarScroll) return
        if (currentStep >= firstCustomizationStep && currentStep <= getLastCustomizationStep()) {
            var idx = currentStep - firstCustomizationStep
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
