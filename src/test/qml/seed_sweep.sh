#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Run each chaos storm across many seeds.
#
# Every tst_chaos_*.qml drives the interface with a pseudo-random sequence of
# clicks, keys and resizes, and ships with two seeds so a normal run is quick
# and reproducible. Two sequences are not many. The storms take
# RPI_CHAOS_SEED so a failure can be replayed, and the same door lets a sweep
# walk a lot more of them.
#
# What it is for is not finding more product defects -- it is finding storms
# that prove less than they claim. A storm asserts something like "no system
# drive became the destination", which says nothing on a run where nothing
# was chosen at all; the suites carry guards against exactly that, and a
# guard only fires on the seeds that reach it. Two sweeps of ten seeds found
# two such storms:
#
#   tst_chaos_storage   chose no drive at all on four seeds in ten, so its
#                       central check held over an empty set.
#   tst_chaos_monkey    saw the screen-reader flag one way only on two seeds
#                       in ten, because the flag is polled rather than
#                       signalled and the storm recorded it straight after
#                       flipping it.
#
# Both are fixed; the sweep is here so the next one is found the same way.
#
# Point it at any build of qml_ui_test, including a sanitised one: the two
# techniques compose, and a lifetime fault on a rarely-taken path needs
# both -- the seeds to reach it and the sanitiser to see it.
#
# Usage: seed_sweep.sh /path/to/qml_ui_test [seed ...]
#
#   RPI_SWEEP_SUITES=<names>   space-separated storm names (default: all of them)
#
# Exits 0 when every seed passes, 1 on any failure.

set -u

BIN=${1:-}
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "usage: $(basename "$0") /path/to/qml_ui_test [seed ...]" >&2
    exit 2
fi
shift

HERE=$(cd "$(dirname "$0")" && pwd)
SEEDS=${*:-101 2029 33331 444449 5555557 66 777 8888 99991 123457}

if [ -n "${RPI_SWEEP_SUITES:-}" ]; then
    SUITES=$RPI_SWEEP_SUITES
else
    SUITES=$(cd "$HERE" && ls tst_chaos_*.qml | sed 's/\.qml$//' | tr '\n' ' ')
fi

# Offscreen and software: a sweep is long enough without a compositor in it,
# and BROWSER=true keeps a stray link from opening one.
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}
export QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software}
export BROWSER=${BROWSER:-true}

# Only read by an ASan build, and there it is what makes the sweep usable: a
# storm draws names holding characters the default font has no glyph for, and
# the fallback fontconfig builds for them is reported as a leak. Thirteen
# seeds in thirty, before this.
export LSAN_OPTIONS=${LSAN_OPTIONS:-suppressions=$HERE/lsan-qt.supp}

# Every XDG directory into a scratch tree beside the binary, and removed
# after. The suites already ask Qt for test-mode paths, which keeps them out
# of the real application's settings -- but test mode still lands under the
# home directory, and a sweep is hundreds of runs. Two hundred and twenty of
# them took about 190 MB, which is a lot to leave in somebody's ~/.local
# because they asked a question about seeds.
SCRATCH=$(mktemp -d "$(dirname "$BIN")/seed-sweep-XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT INT TERM
mkdir -p "$SCRATCH"/{cache,data,state,config}
export XDG_CACHE_HOME=$SCRATCH/cache
export XDG_DATA_HOME=$SCRATCH/data
export XDG_STATE_HOME=$SCRATCH/state
export XDG_CONFIG_HOME=$SCRATCH/config

failures=0
for suite in $SUITES; do
    file="$HERE/$suite.qml"
    [ -f "$file" ] || { echo "no such storm: $suite" >&2; failures=$((failures + 1)); continue; }
    for seed in $SEEDS; do
        # A budget per seed. One seed ran 2563 seconds at full CPU against
        # about eight for its twenty-nine siblings, and with nothing bounding
        # it the sweep behind it stopped for forty-two minutes. This turns
        # that into one reported line. Generous rather than tight: a storm
        # under a sanitiser on a loaded machine is legitimately slow.
        out=$(RPI_CHAOS_SEED="$seed" timeout "${RPI_SWEEP_TIMEOUT:-600}" \
                  "$BIN" -input "$file" 2>&1)
        rc=$?
        # Exit status as well as the reported failures. A storm run under a
        # sanitiser dies on the report rather than printing one, and a plain
        # crash prints nothing at all -- so a sweep that only read stdout for
        # FAIL called both of those a pass.
        # "runtime error:" as well: a UBSan build prints it and carries on,
        # so the case passes and the process exits 0. Neither of the other
        # two conditions can see that, and a sweep is the only place these
        # storms meet a sanitiser.
        if [ "$rc" -ne 0 ] || printf '%s' "$out" | grep -qE '^FAIL|runtime error:'; then
            failures=$((failures + 1))
            echo "FAILED $suite seed=$seed (exit $rc)"
            [ "$rc" -eq 124 ] && echo "  over the ${RPI_SWEEP_TIMEOUT:-600}s budget"
            # The whole output, kept beside the sweep's own log. A sanitiser
            # report is thirty frames and the filter below keeps five, which
            # twice left a failure that could not be explained and did not
            # reproduce on demand.
            if [ -n "${RPI_SWEEP_KEEP:-}" ]; then
                mkdir -p "$RPI_SWEEP_KEEP"
                printf '%s\n' "$out" > "$RPI_SWEEP_KEEP/$suite-seed$seed.log"
                echo "  full output: $RPI_SWEEP_KEEP/$suite-seed$seed.log"
            fi
            detail=$(printf '%s\n' "$out" \
                | grep -E '^FAIL|ERROR: (Address|Leak|UndefinedBehavior)Sanitizer|runtime error:|SUMMARY:' \
                | head -5)
            if [ -n "$detail" ]; then
                printf '%s\n' "$detail"
            else
                # A non-zero exit with no reported failure and no sanitiser
                # report happened once, under load, and left nothing to look
                # at. The last few lines are better than silence.
                echo "  (no failure or sanitiser report; last lines follow)"
                printf '%s\n' "$out" | tail -5 | sed 's/^/  /'
            fi
        fi
    done
    echo "$suite: swept $(printf '%s' "$SEEDS" | wc -w) seeds"
done

echo "seed failures: $failures"
[ "$failures" -eq 0 ]
