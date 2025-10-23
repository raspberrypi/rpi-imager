/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls

TextField {
    id: root

    // Sensible defaults to ensure consistent behavior across the app
    activeFocusOnPress: true
    activeFocusOnTab: true
    selectByMouse: true
    cursorVisible: activeFocus
    
    // Accessibility properties
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText !== "" ? placeholderText : text
    Accessible.description: ""
    Accessible.editable: true
    Accessible.focused: activeFocus
    Accessible.passwordEdit: echoMode === TextInput.Password

    // No special key handling here; rely on WizardStepBase auto wiring
}




