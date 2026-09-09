/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
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
 *
 * An *intent* confirmation asks for a positive act, which is always typing
 * something: the drive's name before writing to a system disk, the device
 * serial before programming OTP fuses, the countdown before the write
 * itself begins. Typing the name of the thing you are about to destroy is
 * a statement of intent rather than an interruption, and there is nowhere
 * else to put it -- skipping it would remove the confirmation rather than
 * relocate it. Those are never skipped, for anybody, and do not use this.
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
     *
     * A caller that skips on this must carry the dialog's warning in the
     * control's own Accessible.description, so the content is relocated to
     * where it is spoken rather than dropped. That is the condition on
     * skipping at all, not a nicety.
     */
    function shouldConfirm(warningsDisabled) {
        if (warningsDisabled)
            return false
        return !PlatformHelper.assistiveTechnologyActive
    }
}
