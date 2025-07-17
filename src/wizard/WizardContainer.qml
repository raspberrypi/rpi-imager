/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../qmlcomponents"

import RpiImager

Item {
    id: root
    
    required property ImageWriter imageWriter
    property var optionsPopup: null
    
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
    
    // Main horizontal layout
    RowLayout {
        anchors.fill: parent
        spacing: 0
        
        // Sidebar
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: "#C51E3A"
            border.color: "#A91B2E"
            border.width: 1
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 5
                
                // Header
                Text {
                    text: qsTr("Setup Steps")
                    font.pixelSize: 16
                    font.family: Style.fontFamilyBold
                    font.bold: true
                    color: "white"
                    Layout.fillWidth: true
                    Layout.bottomMargin: 10
                }
                
                // Step list
                Repeater {
                    model: root.stepNames
                    
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 35
                        color: index === root.getSidebarIndex(root.currentStep) ? "#2196F3" : "transparent"
                        border.color: index === root.getSidebarIndex(root.currentStep) ? "#2196F3" : "transparent"
                        border.width: 1
                        radius: 4
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            
                            // Step name (no numbering for cleaner look)
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                font.pixelSize: 13
                                font.family: Style.fontFamily
                                color: "white"
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                
                // Spacer
                Item {
                    Layout.fillHeight: true
                }
                
                // Advanced options button
                Rectangle {
                    id: optionsButtonRect
                    Layout.fillWidth: true
                    Layout.preferredHeight: 35
                    color: optionsButton.containsMouse ? Qt.rgba(255,255,255,0.1) : "transparent"
                    border.color: Qt.rgba(255,255,255,0.3)
                    border.width: 1
                    radius: 4
                    
                    MouseArea {
                        id: optionsButton
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.optionsPopup) {
                                root.optionsPopup.initialize()
                                root.optionsPopup.show()
                            }
                        }
                    }
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        
                        // Cog icon
                        Image {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            source: "../icons/ic_cog_red.svg"
                            sourceSize.width: 16
                            sourceSize.height: 16
                        }
                        
                        // Label
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Advanced Options")
                            font.pixelSize: 11
                            font.family: Style.fontFamily
                            color: "white"
                            elide: Text.ElideRight
                        }
                    }
                }
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
            root.currentStep++
            var nextComponent = getStepComponent(root.currentStep)
            if (nextComponent) {
                wizardStack.push(nextComponent)
            }
        }
    }
    
    function previousStep() {
        if (root.currentStep > 0) {
            root.currentStep--
            wizardStack.pop()
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
            showBackButton: false
            // Let WritingStep handle its own button text based on state
            onNextClicked: {
                // Only advance if the step indicates it's ready
                if (isComplete) {
                    root.nextStep()
                }
                // Otherwise, let the step handle the action internally
            }
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
} 