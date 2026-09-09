/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Working out what changed between two versions of a list model's rows.
 *
 * A model that answers every update with beginResetModel() destroys every
 * delegate in the view. That costs more than the rebuilding: the scroll
 * position and the highlight go with it, and so does any click in progress.
 * Qt delivers a click only when the press and the release reach the same
 * item, so a list rebuilt between the two swallows it -- the user presses a
 * row, the list refills, nothing is selected, and they have to click again.
 *
 * That was reachable in the OS list. Until the list arrives the model holds
 * exactly two rows, "Erase" and "Use custom", and the arrival of the real
 * list is what rebuilds it, so anyone who reaches the screen before the
 * fetch lands is clicking on a list that is about to be replaced.
 *
 * The arithmetic below is the shared part: given the keys of the rows a
 * model has and the keys of the rows it should have, it says which rows to
 * remove and which to insert. The signalling stays with each model, because
 * beginRemoveRows and its siblings are protected members of
 * QAbstractItemModel and cannot be called from anywhere else.
 *
 * Used by the OS list and the board list, which are both positional: rows
 * arrive and leave in runs, and the ones either side keep their place.
 *
 * DriveListModel is deliberately not one of them. It holds its rows in a
 * sorted map and already inserts and removes them one at a time at the
 * position the sort puts them, which is the right shape for a list where
 * drives appear and disappear anywhere in it. Describing that as one
 * contiguous run, which is all this reports, would replace rows that
 * currently survive -- the opposite of the point.
 */

#ifndef MODEL_ROW_DIFF_H
#define MODEL_ROW_DIFF_H

#include <QStringList>

namespace rpi_model {

/*
 * One contiguous removal and one contiguous insertion, both at the same
 * position -- which is all that is needed for the way these lists change:
 * entries appear or disappear in a run, and the rows either side of that run
 * keep their identity.
 *
 * A list that has been reordered rather than added to falls back to
 * replacing the part that moved, which is correct but not minimal. Doing
 * better would mean a full edit-distance pass, and nothing here reorders.
 */
struct RowDiff {
    int at = 0;        // where the change starts
    int removed = 0;   // rows to take out at `at`
    int inserted = 0;  // rows to put in at `at`, after the removal

    bool isEmpty() const { return removed == 0 && inserted == 0; }
};

/*
 * Compare two lists of row keys.
 *
 * A key is whatever makes two rows the same row rather than merely
 * equal-looking -- a url and a name, a device path. Contents are not
 * compared here: rows that keep their key and change their contents are the
 * caller's business, and are reported with dataChanged rather than by being
 * replaced.
 */
RowDiff planRowDiff(const QStringList &currentKeys, const QStringList &nextKeys);

} // namespace rpi_model

#endif // MODEL_ROW_DIFF_H
