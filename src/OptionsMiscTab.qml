/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "qmlcomponents"

import RpiImager

OptionsTabBase {
    id: root

    property alias beepEnabled: chkBeep.checked
    property alias telemetryEnabled: chkTelemtry.checked
    property alias ejectEnabled: chkEject.checked

    // Add property aliases to expose checkboxes for focus management
    property alias chkBeep: chkBeep
    property alias chkEject: chkEject
    property alias chkTelemtry: chkTelemtry

    ColumnLayout {
        // Ensure layout doesn't interfere with tab navigation
        activeFocusOnTab: false
        
        ImCheckBox {
            id: chkBeep
            text: qsTr("Play sound when finished")
            
            // Handle explicit navigation in both directions
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                    chkEject.forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier)) {
                    // Navigate back to TabBar
                    if (root.tabBar) {
                        root.tabBar.forceActiveFocus()
                        event.accepted = true
                    }
                }
            }
        }
        ImCheckBox {
            id: chkEject
            text: qsTr("Eject media when finished")
            
            // Handle explicit navigation in both directions
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                    chkTelemtry.forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier)) {
                    chkBeep.forceActiveFocus()
                    event.accepted = true
                }
            }
        }
        ImCheckBox {
            id: chkTelemtry
            text: qsTr("Enable telemetry")
            
            // Handle explicit navigation in both directions
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                    // Navigate to Cancel/Save buttons
                    if (root.navigateToButtons) {
                        root.navigateToButtons()
                        event.accepted = true
                    }
                } else if (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier)) {
                    chkEject.forceActiveFocus()
                    event.accepted = true
                }
            }
        }
    }
}
