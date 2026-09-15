// Fuzz the config.txt merge, and check the property the code promises.
//
// config.txt comes off the image being written, and the merge rewrites it
// before the card is finished. The header documents three outcomes, and two
// of them are invariants worth holding to: a setting already present must
// leave the file untouched, and merging the same item twice must be the
// same as merging it once. A merge that grew the file every time would
// still "work" and would slowly fill a boot partition.
#include "config_txt_merge.h"
#include "fuzz_silence.h"

#include <QByteArray>

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size > 64 * 1024)
        return 0;

    // Split the input: a config body and the item to merge into it.
    const size_t split = size_t(data[0]) * (size - 1) / 255;
    const QByteArray config(reinterpret_cast<const char *>(data + 1),
                            int(split));
    const QByteArray item(reinterpret_cast<const char *>(data + 1 + split),
                          int(size - 1 - split));

    // The caller splits its customisation on '\n' before calling, so an
    // item never contains one. Feeding a multi-line item breaks idempotence
    // trivially -- a config line can never equal it, so it appends forever --
    // and that is outside the contract rather than a defect. A trailing '\r'
    // is left in scope: a CRLF config really can produce one.
    if (item.contains('\n'))
        return 0;

    const QByteArray once = mergeConfigTxtItem(config, item);
    const QByteArray twice = mergeConfigTxtItem(once, item);

    // Idempotence: the second merge must find what the first one wrote.
    if (once != twice)
        __builtin_trap();

    // And the item must actually be in effect afterwards, uncommented,
    // whenever it was something the merge could act on at all. Compared in
    // the same stripped form the merge stores, or a CRLF item would look
    // absent when it is there.
    int wend = item.size();
    while (wend > 0 && item[wend - 1] == '\r') --wend;
    const QByteArray wanted = item.left(wend);
    if (!wanted.isEmpty() && !wanted.contains('\n') && !wanted.startsWith('#')) {
        bool present = false;
        for (const QByteArray &line : once.split('\n')) {
            int lend = line.size();
            while (lend > 0 && line[lend - 1] == '\r') --lend;
            const QByteArray l = line.left(lend);
            if (l == wanted) { present = true; break; }
        }
        if (!present)
            __builtin_trap();
    }
    return 0;
}
