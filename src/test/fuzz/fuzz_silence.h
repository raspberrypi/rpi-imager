// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Throw away Qt's logging for the duration of a fuzz run.
//
// Several subjects here narrate what they do -- every table recognised,
// every OS entry pruned. Useless under a fuzzer calling them millions of
// times: a 23-minute soak wrote 1.4 MB of "Using GPT partition table" and
// 7.3 MB of pruning notices.
//
// Not a throughput fix. Measured either way the rate is the same within
// noise; those harnesses are slow because of their work, not their output.
//
// Installed by construction, so including the header is the whole of it.
// Sanitiser and libFuzzer diagnostics go to stderr and are unaffected.

#ifndef RPI_FUZZ_SILENCE_H
#define RPI_FUZZ_SILENCE_H

#include <QtGlobal>
#include <QString>

namespace {

struct FuzzSilencer {
    FuzzSilencer()
    {
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &,
                                  const QString &) {});
    }
};

const FuzzSilencer g_fuzzSilencer;

} // namespace

#endif // RPI_FUZZ_SILENCE_H
