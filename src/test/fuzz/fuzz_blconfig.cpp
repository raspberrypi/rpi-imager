// Fuzz the bootloader configuration read out of the EEPROM.
//
// The imager looks in the bootloader's own flash for IMAGER_REPO_URL and, if
// it finds one, uses it as the OS list repository. What it reads is an nvmem
// region: the text a person wrote, then whatever the flash held before --
// padding, the tail of an older configuration, arbitrary bytes.
//
// Two properties. Whatever comes back has to be something that really was
// behind that key, so a run over the region cannot invent one out of the
// padding; and the walk itself has to cope with the shapes flash produces --
// no newline at all, embedded NULs, a key split across the end of the
// region, hundreds of thousands of lines.
#include "eeprom_repo_override.h"
#include "fuzz_silence.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QString>

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 64 * 1024)
        size = 64 * 1024;

    const QByteArray blconfig(reinterpret_cast<const char *>(data),
                              static_cast<qsizetype>(size));
    const QString found = rpi_eeprom::repoUrlFromBlconfig(blconfig);
    if (found.isEmpty())
        return 0;

    // It said it found one. Some line has to have carried it.
    //
    // Compared after a normalisation rather than by re-deriving the value,
    // and the difference matters. Two attempts at re-deriving it disagreed
    // with the implementation in opposite directions, over one thing:
    // QString::fromUtf8() removes a byte order mark only when the mark is
    // the first thing it is handed. Trim the bytes first and the mark moves
    // to the front and is dropped; trim afterwards and it stays. Neither
    // order is wrong -- but a harness that picks one is asserting how the
    // answer was computed, which is not what it is for.
    //
    // So: drop the marks, cut at the padding a region is filled with, trim,
    // and ask only that what came back is what some line carrying the key
    // says. That still catches a value invented out of the padding, which
    // is the property this target exists for.
    static const QByteArray key = QByteArrayLiteral("IMAGER_REPO_URL=");
    const auto normalise = [](QString s) {
        s.remove(QChar(0xFEFF));
        const qsizetype nul = s.indexOf(QChar(0x0000));
        if (nul >= 0)
            s.truncate(nul);
        const qsizetype bad = s.indexOf(QChar(0xFFFD));
        if (bad >= 0)
            s.truncate(bad);
        return s.trimmed();
    };
    const QString wanted = normalise(found);

    bool accountedFor = false;
    const QByteArrayList lines = blconfig.split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith(key))
            continue;
        if (normalise(QString::fromUtf8(line.mid(key.size()))) == wanted) {
            accountedFor = true;
            break;
        }
    }
    if (!accountedFor)
        __builtin_trap();

    // And it is a value, not the whitespace and padding around one.
    if (found != found.trimmed() || found.isEmpty())
        __builtin_trap();

    // Not asserted: that the value carries no replacement character. It was
    // tried and it is the wrong question -- a replacement character stands
    // for any byte that is not valid UTF-8, so it appears for a stray 0xC3
    // in the middle of a value as readily as for the 0xFF a region is
    // erased with, and only the second is padding. A URL carrying one is
    // refused by isHttpUrl() downstream, where the decision belongs.

    return 0;
}
