/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import RpiImager

/*
 * Two-column label/field grid shared by the customisation steps.
 *
 * The label column is only as wide as the widest label, and long translations
 * can push that far enough to leave the fields with almost nothing to work
 * with - Russian is the worst case. The window's 680px minimum is deliberate
 * for low-resolution displays, so the room has to come out of the layout
 * instead: once the labels would take more than `stackRatio` of the row, the
 * grid drops to a single column and each label sits above its own field, which
 * hands the full width back to the fields.
 *
 * The switch is driven by the measured label widths rather than by a list of
 * languages, so it holds for translations added or reworded later.
 */
GridLayout {
    id: root

    // Share of the available width the label column may take before the labels
    // move above their fields.
    property real stackRatio: 0.45

    // What the label column actually costs: the widest label in the grid.
    readonly property real labelColumnWidth: {
        var widest = 0
        for (var i = 0; i < root.children.length; ++i) {
            var child = root.children[i]
            if (child && child.visible && child.isWizardFormLabel === true)
                widest = Math.max(widest, child.implicitWidth)
        }
        return widest
    }

    readonly property bool stacked: root.width > 0
                                    && root.labelColumnWidth > root.width * root.stackRatio

    columns: stacked ? 1 : 2
    columnSpacing: Style.formColumnSpacing
    rowSpacing: stacked ? Style.spacingSmall : Style.formRowSpacing
}
