/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2

TextField {
    id: root

    // Sensible defaults to ensure consistent behavior across the app
    activeFocusOnPress: true
    activeFocusOnTab: true
    selectByMouse: true
    cursorVisible: activeFocus

    // No special key handling here; rely on WizardStepBase auto wiring
}




