/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "model_row_diff.h"

namespace rpi_model {

RowDiff planRowDiff(const QStringList &currentKeys, const QStringList &nextKeys)
{
    const int oldCount = currentKeys.size();
    const int newCount = nextKeys.size();

    // How much of each end is the same rows in the same order. Whatever sits
    // between the two is what actually changed.
    int prefix = 0;
    while (prefix < oldCount && prefix < newCount
           && currentKeys.at(prefix) == nextKeys.at(prefix)) {
        ++prefix;
    }

    int suffix = 0;
    while (suffix < oldCount - prefix && suffix < newCount - prefix
           && currentKeys.at(oldCount - 1 - suffix)
              == nextKeys.at(newCount - 1 - suffix)) {
        ++suffix;
    }

    RowDiff diff;
    diff.at = prefix;
    diff.removed = oldCount - prefix - suffix;
    diff.inserted = newCount - prefix - suffix;
    return diff;
}

} // namespace rpi_model
