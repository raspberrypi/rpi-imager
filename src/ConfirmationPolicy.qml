/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

pragma Singleton

import QtQuick
import RpiImager

/**
 * Whether to interrupt the user with a confirmation dialog.
 *
 * There are two kinds of confirmation in this application and they are not
 * interchangeable.
 *
 * An *understanding* confirmation explains a consequence and offers two
 * buttons. Disabling the system drive filter, enabling passwordless sudo,
 * turning on USB gadget mode. Nothing is asked of the user except that they
 * read it; the dialog exists to slow down someone who has not registered
 * what the control does. Those go through shouldConfirm() below.
 */
QtObject {
    /**
     * True when an understanding confirmation should be raised.
     *
     * `warningsDisabled` is the deployment-wide opt-out from the app options
     * dialog. The other condition is an assistive technology being attached:
     * by the time a screen reader user activates a control they have been
     * read its label, its role and its accessible description, which is more
     * than a sighted mouse user is ever shown. Raising the dialog would
     * interrupt the person who was told the most in order to protect the one
     * who was told the least.
     */
    function shouldConfirm(warningsDisabled) {
        if (warningsDisabled)
            return false
        return !PlatformHelper.assistiveTechnologyActive
    }
}
