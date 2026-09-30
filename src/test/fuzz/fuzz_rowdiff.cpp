// Fuzz the diff that rebuilds a list model in place.
//
// The board list and the OS list are refilled from the repository without
// resetting the model, so a click in progress survives the refresh. What
// changed is worked out by planRowDiff() over the two lists of row keys, and
// the caller then removes a run, inserts a run, and walks the result copying
// contents across by index:
//
//     for (int i = 0; i < live.size(); ++i)
//         if (!sameContents(live.at(i), next.at(i))) ...
//
// which reads past the end of `next` the moment the diff disagrees with
// itself. A key is a board's name, and nothing stops a repository serving
// two boards with one name, so the lists here are drawn from a tiny alphabet
// to make repeats and near-misses the common case rather than the rare one.
#include "model_row_diff.h"
#include "fuzz_silence.h"

#include <QStringList>

#include <cstdint>

namespace {

// Two lists out of one buffer: a length byte, then one byte per key. Four
// distinct keys, so prefixes and suffixes match by accident constantly --
// which is what the scan at either end of the diff is looking for.
QStringList takeList(const uint8_t *&data, size_t &size)
{
    QStringList out;
    if (size == 0)
        return out;
    int count = int(data[0]) % 33;   // up to 32 rows
    ++data;
    --size;
    for (int i = 0; i < count && size > 0; ++i) {
        out << QString(QLatin1Char('a' + char(data[0] % 4)));
        ++data;
        --size;
    }
    return out;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 128)
        size = 128;

    const QStringList current = takeList(data, size);
    const QStringList next = takeList(data, size);

    const rpi_model::RowDiff diff = rpi_model::planRowDiff(current, next);

    // Every index the caller forms has to be one it may use.
    if (diff.at < 0 || diff.removed < 0 || diff.inserted < 0)
        __builtin_trap();
    if (diff.at + diff.removed > current.size())
        __builtin_trap();
    if (diff.at + diff.inserted > next.size())
        __builtin_trap();

    // The one that matters: after the removal and the insertion the live list
    // is the same length as the incoming one, so the copy loop stays inside
    // both.
    if (current.size() - diff.removed + diff.inserted != next.size())
        __builtin_trap();

    // And doing what the caller does has to arrive at the incoming list
    // exactly, or rows keep their contents while their identity moves.
    QStringList live = current;
    for (int i = 0; i < diff.removed; ++i)
        live.removeAt(diff.at);
    for (int i = 0; i < diff.inserted; ++i)
        live.insert(diff.at + i, next.at(diff.at + i));
    for (int i = 0; i < live.size(); ++i) {
        if (i >= diff.at && i < diff.at + diff.inserted)
            continue;
        live[i] = next.at(i);
    }
    if (live != next)
        __builtin_trap();

    // An unchanged list must not be reported as a change: a diff that always
    // replaced everything would satisfy every check above and destroy the
    // delegates this exists to keep.
    if (current == next && !diff.isEmpty())
        __builtin_trap();

    // And the same worry for a list that did change, which the check above
    // cannot see: replacing every row satisfies all of it and swallows the
    // click, which is the whole reason this file exists. Rows before the
    // first disagreement hold the same key at the same index in both lists,
    // so removing one destroys a delegate that had no reason to go.
    //
    // Removal only. An insertion above a row moves its delegate rather than
    // destroying it, and the header allows a reordered list to fall back to
    // replacing the part that moved -- which is a statement about the
    // middle, not about the leading scan.
    int prefix = 0;
    while (prefix < current.size() && prefix < next.size()
           && current.at(prefix) == next.at(prefix))
        ++prefix;
    if (diff.removed > 0 && diff.at < prefix)
        __builtin_trap();       // a row that did not change, taken out

    return 0;
}
