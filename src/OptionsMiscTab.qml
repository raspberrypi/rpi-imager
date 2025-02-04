/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "qmlcomponents"

ScrollView {
    id: root

    property alias beepEnabled: chkBeep.checked
    property alias telemetryEnabled: chkTelemtry.checked
    property alias ejectEnabled: chkEject.checked

    font.family: roboto.name

    Layout.fillWidth: true
    Layout.fillHeight: true

    property double scrollPosition

    // clip: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    ScrollBar.vertical.position: scrollPosition
    scrollPosition: scrollPosition

    ColumnLayout {
        ImCheckBox {
            id: chkBeep
            text: qsTr("Play sound when finished")
        }
        ImCheckBox {
            id: chkEject
            text: qsTr("Eject media when finished")
        }
        ImCheckBox {
            id: chkTelemtry
            text: qsTr("Enable telemetry")
        }
    }
}
