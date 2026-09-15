// Fuzz the quoting the customisation generator rests on.
//
// firstrun.sh runs as root on first boot, built from text the user typed.
// If any of it escapes its quoting, the result is arbitrary root code on the
// card. shellQuote() stands between the two, so it is checked as a property:
// decode what it produced the way a shell would, and require the original
// string back.
//
// The generators are fuzz_customisation_gen, because they are four orders
// of magnitude slower and held this back: twenty thousand runs here take
// under a second, and the same runs with the generators took minutes.
#include "customization_generator.h"
#include "fuzz_silence.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <string>

namespace {

// Undo POSIX quoting: concatenate '...' and "..." runs and bare characters,
// which is what a shell does when it reads a word.
bool shellUnquote(const QString &in, QString *out)
{
    QString acc;
    int i = 0;
    while (i < in.size()) {
        const QChar c = in.at(i);
        if (c == u'\'') {
            ++i;
            while (i < in.size() && in.at(i) != u'\'')
                acc.append(in.at(i++));
            if (i >= in.size())
                return false;          // unterminated single quote
            ++i;
        } else if (c == u'"') {
            ++i;
            while (i < in.size() && in.at(i) != u'"') {
                if (in.at(i) == u'\\' && i + 1 < in.size())
                    ++i;
                acc.append(in.at(i++));
            }
            if (i >= in.size())
                return false;          // unterminated double quote
            ++i;
        } else {
            acc.append(c);
            ++i;
        }
    }
    *out = acc;
    return true;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192)
        return 0;

    const QString value = QString::fromUtf8(reinterpret_cast<const char *>(data),
                                            int(size));

    // The property: quoting is reversible, whatever went in.
    const QString quoted = rpi_imager::CustomisationGenerator::shellQuote(value);
    QString roundTripped;
    if (!shellUnquote(quoted, &roundTripped))
        __builtin_trap();              // the shell could not even parse it
    if (roundTripped != value)
        __builtin_trap();              // it parsed as something else

    // The two scrubbers that guard the same strings on the way in. Both are
    // as cheap as the property and belong with it.
    (void)rpi_imager::CustomisationGenerator::stripLineTerminators(value);
    (void)rpi_imager::CustomisationGenerator::yamlEscapeSsidOctets(
        QByteArray(reinterpret_cast<const char *>(data), int(size)));
    return 0;
}
