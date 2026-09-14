// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Fuzz the customisation generators themselves.
//
// firstrun.sh runs as root on first boot, and the cloud-init YAML does the
// same job elsewhere. Every field in them came from a text box. This puts
// one hostile string in all of them and asks that nothing falls over.
//
// Split from fuzz_customisation because of the cost: these derive the
// account credential and yescrypt is memory-hard. Sampling inside one
// target was tried and failed -- one case in sixteen gave forty executions
// a second, one in 256 only sixty, because a sampled call costs more than
// a thousand unsampled ones. Slow, so give it hours.

#include "customization_generator.h"
#include "fuzz_silence.h"

#include <QByteArray>
#include <QString>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>
#include <QMetaType>

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Longer than any of these fields can be, and long enough that the
    // wrapping and escaping in the generators has something to work on.
    if (size > 4096)
        return 0;

    const QString value = QString::fromUtf8(reinterpret_cast<const char *>(data),
                                            int(size));

    // The same string everywhere a user can put one, so a single input
    // exercises every field rather than one at a time.
    QVariantMap s;
    s["hostname"] = value;
    s["sysname"] = value;
    s["password"] = value;
    s["wifiSSID"] = value;
    // PBKDF2 is 4096 rounds of HMAC-SHA1 and it runs several times per
    // input, which held this target to eleven executions a second.
    //
    // What it guards is not what this target is for. A passphrase of a
    // length that gets derived (8..63) comes out as hex either way, so the
    // quoting downstream sees hex whether the derivation ran or not; a
    // value outside that range is passed through raw, and that is the case
    // where the quoting has something to do -- and it never enters PBKDF2.
    // Handing it a PSK already derived skips the rounds without losing the
    // escaping this target exists to exercise.
    //
    // Not always, because the derivation and the length test around it are
    // ours too. One input in sixteen takes the plaintext path.
    if ((size % 16) == 0)
        s["wifiPassword"] = value;
    else
        s["wifiPasswordCrypt"] = QStringLiteral(
            "0123456789abcdef0123456789abcdef"
            "0123456789abcdef0123456789abcdef");
    s["wifiCountry"] = value;
    s["timezone"] = value;
    s["keyboardLayout"] = value;
    s["sshAuthorizedKeys"] = value;
    s["sshEnabled"] = true;
    s["wifiSSIDHidden"] = false;

    (void)rpi_imager::CustomisationGenerator::generateSystemdScript(s, value);
    (void)rpi_imager::CustomisationGenerator::generateCloudInitUserData(s, value);
    (void)rpi_imager::CustomisationGenerator::generateCloudInitNetworkConfig(s);

    // The same map with the toggles the other way round: the hidden-SSID
    // branch and the no-SSH branch are different code, and a run that only
    // ever sees one set of flags never reaches them.
    s["sshEnabled"] = false;
    s["wifiSSIDHidden"] = true;
    (void)rpi_imager::CustomisationGenerator::generateSystemdScript(s, value);
    (void)rpi_imager::CustomisationGenerator::generateCloudInitNetworkConfig(s);

    // firstrun.sh runs as root, and a here-document in it ends at the first
    // line equal to its delimiter. Every one of them carries something a
    // user typed, so a value holding a line "EOF" closed the document early
    // and everything after it became script. An authorized_keys value of
    // "ssh-rsa AAAA\nEOF\nrm -rf /" produced exactly that.
    //
    // So: for every document the script opens, the value must not contain a
    // line equal to its delimiter. Said any other way it was unsound. As a
    // line count, twice -- a value stripped to nothing takes a different
    // branch, and so does one the field refuses, so the script legitimately
    // comes out a different length. As "the raw value must not appear", it
    // accused the SSID, which is single-quoted, and inside single quotes a
    // newline is a character like any other. As "and must be quoted either
    // side", it accused the keys, which sit in a here-document -- which is
    // the very thing this is about.
    if (value.contains(u'\n') || value.contains(u'\r')) {
        const QString flat =
            rpi_imager::CustomisationGenerator::stripLineTerminators(value);
        if (flat.contains(u'\n') || flat.contains(u'\r'))
            __builtin_trap();          // the scrubber left one in

        const QString script = QString::fromUtf8(
            rpi_imager::CustomisationGenerator::generateSystemdScript(s, value));
        const QStringList valueLines = value.split(u'\n');

        static const QRegularExpression opener(QStringLiteral("<<'([^']*)'"));
        auto it = opener.globalMatch(script);
        while (it.hasNext()) {
            const QString delimiter = it.next().captured(1);
            if (delimiter.isEmpty())
                continue;
            if (valueLines.contains(delimiter))
                __builtin_trap();      // the value can close the document
        }
    }

    // The same hazard in the other generator. cloud-init's user-data is
    // YAML, and a scalar written bare ends at the newline in it -- so a
    // hostname of "pi\nruncmd:\n - touch /tmp/pwned" put a runcmd key in the
    // document, and cloud-init runs those as root on first boot. hostname,
    // timezone and the user's name were all written bare.
    //
    // A value that is escaped properly contributes no line of its own: its
    // newlines become \n inside a quoted scalar. So no line of the document
    // may equal a line of the value after the first.
    if (value.contains(u'\n')) {
        const QString yaml = QString::fromUtf8(
            rpi_imager::CustomisationGenerator::generateCloudInitUserData(s, value));
        const QStringList yamlLines = yaml.split(u'\n');
        const QStringList valueLines = value.split(u'\n');

        for (int i = 1; i < valueLines.size(); ++i) {
            if (valueLines.at(i).isEmpty())
                continue;
            if (yamlLines.contains(valueLines.at(i)))
                __builtin_trap();      // the value became a line of the document
        }

        // And the third document, whose result this target used to throw
        // away. The network config is YAML too, and the input above is the
        // wifi SSID, the country and -- one input in sixteen -- the
        // passphrase. A bare scalar ends at a newline there exactly as it
        // does in user-data, and cloud-init reads this one as root on the
        // same first boot.
        const QString net = QString::fromUtf8(
            rpi_imager::CustomisationGenerator::generateCloudInitNetworkConfig(s));
        const QStringList netLines = net.split(u'\n');
        for (int i = 1; i < valueLines.size(); ++i) {
            if (valueLines.at(i).isEmpty())
                continue;
            if (netLines.contains(valueLines.at(i)))
                __builtin_trap();      // the value became a line of the network config
        }
    }

    return 0;
}
