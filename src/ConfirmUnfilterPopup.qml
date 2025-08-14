// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Raspberry Pi Ltd

import QtQuick.Layouts 1.15

MsgPopup {
    id: root
    title: qsTr("Remove system drive filter?")
    continueButton: false

    signal confirmed()
    signal cancelled()

    readonly property string riskText: CommonStrings.warningRiskText
    readonly property string proceedText: CommonStrings.warningProceedText
    // Track how the popup was dismissed so the close button/Esc acts as cancel
    property string _result: ""

    // Provide implementation for focus navigation expected by base popup
    getNextFocusableElement: function(startElement) {
        var focusableItems = [showButton, cancelButton, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })
        if (focusableItems.length === 0) return startElement;
        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[0];
        var nextIndex = (currentIndex + 1) % focusableItems.length;
        return focusableItems[nextIndex];
    }

    getPreviousFocusableElement: function(startElement) {
        var focusableItems = [showButton, cancelButton, root.closeButton].filter(function(item) {
            return item.visible && item.enabled
        })
        if (focusableItems.length === 0) return startElement;
        var currentIndex = focusableItems.indexOf(startElement)
        if (currentIndex === -1) return focusableItems[0];
        var prevIndex = (currentIndex - 1 + focusableItems.length) % focusableItems.length;
        return focusableItems[prevIndex];
    }

    text: qsTr("By disabling system drive filtering, <b>system drives will be shown</b> in the list.") + "<br><br>" + root.riskText + "<br><br>" + root.proceedText

    RowLayout {
        Layout.alignment: Qt.AlignCenter | Qt.AlignBottom
        Layout.bottomMargin: 10
        spacing: 20

        ImButtonRed {
            id: cancelButton
            text: qsTr("KEEP FILTER ON")
            onClicked: {
                root._result = "cancelled"
                root.close()
            }
        }

        ImButton {
            id: showButton
            text: qsTr("SHOW SYSTEM DRIVES")
            onClicked: {
                root._result = "confirmed"
                root.close()
            }
        }
    }

    onOpened: {
        _result = ""
    }

    onClosed: {
        if (_result === "confirmed") {
            root.confirmed()
        } else {
            // Close via cancel button, title close button, or Esc
            root.cancelled()
        }
    }
}


