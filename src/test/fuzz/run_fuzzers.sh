#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Run every harness against a corpus that outlives the run.
#
# Each session used to start from the seeds and stop at whatever it reached
# before the clock ran out, so the second hour re-derived the first one. A
# corpus kept between runs makes each session start where the last finished,
# which is the whole reason libFuzzer writes one.
#
# It also gathers the three environment settings the README asks for and that
# a hand-typed command forgets: the FAT template both filesystem harnesses
# need, the container-overflow check turned off because Qt is not instrumented
# here, and the suppressions for the two deliberate wraps.
#
# Usage: run_fuzzers.sh [-b build] [-t seconds] [-j jobs] [-c corpus-root] [target ...]
#
#   -b  build directory holding test/fuzz (default: build-fuzzcm)
#   -t  seconds per target (default: 60)
#   -j  targets to run at once (default: 1). Each libFuzzer is a
#       single process, so this is how a four-core machine stops
#       spending four hours doing one thing at a time.
#   -c  corpus root, kept between runs (default: <build>/fuzz-corpus)
#   -m  minimise each corpus before running it
#
# Exits 0 when no target reported anything, 1 otherwise. Anything a target
# does find is left in <corpus-root>/findings, named for the target that
# found it, so it can be replayed by passing the file to that binary.

set -u

BUILD=build-fuzzcm
SECS=60
CORPUS=
MINIMISE=0
JOBS=1

while getopts "b:t:c:j:mh" opt; do
    case $opt in
        b) BUILD=$OPTARG ;;
        t) SECS=$OPTARG ;;
        c) CORPUS=$OPTARG ;;
        j) JOBS=$OPTARG ;;
        m) MINIMISE=1 ;;
        h) sed -n '18,27p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))

HERE=$(cd "$(dirname "$0")" && pwd)
BINDIR=$BUILD/test/fuzz
if [ ! -d "$BINDIR" ]; then
    echo "no harnesses in $BINDIR -- configure with -DRPI_IMAGER_FUZZERS=ON" >&2
    exit 2
fi
CORPUS=${CORPUS:-$BUILD/fuzz-corpus}
FINDINGS=$CORPUS/findings
mkdir -p "$CORPUS" "$FINDINGS"

# The seeds and the FAT template. Four harnesses cannot get started without
# them and two cannot start at all; make_seeds.py leaves anything already
# built alone, so this is cheap on every run after the first.
export FUZZ_FAT_TEMPLATE=${FUZZ_FAT_TEMPLATE:-$(cd "$CORPUS" && pwd)/template.img}
python3 "$HERE/make_seeds.py" "$CORPUS" >/dev/null || {
    echo "make_seeds.py failed -- dosfstools and mtools are what it needs" >&2
    exit 2
}

# Qt is not built with ASan here, so instrumented and uninstrumented
# containers meet and the check reports what is not a defect.
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_container_overflow=0}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-suppressions=$HERE/ubsan.supp:halt_on_error=0:print_stacktrace=1}

# Where make_seeds.py put the seeds for the harnesses that have them. The rest
# start from an empty directory of their own.
corpus_dir_for() {
    case $1 in
        fuzz_parttable)       echo "$CORPUS/corpus_pt" ;;
        fuzz_fastboot)        echo "$CORPUS/corpus_fb" ;;
        fuzz_sparse_roundtrip) echo "$CORPUS/corpus_sparse" ;;
        fuzz_imagesize)       echo "$CORPUS/corpus_imagesize" ;;
        fuzz_bmap)            echo "$CORPUS/corpus_bmap" ;;
        fuzz_fileserver)      echo "$CORPUS/corpus_fileserver" ;;
        *)                    echo "$CORPUS/corpus_${1#fuzz_}" ;;
    esac
}

if [ "$#" -gt 0 ]; then
    TARGETS=$*
else
    TARGETS=$(find "$BINDIR" -maxdepth 1 -type f -executable -name 'fuzz_*' \
              -printf '%f\n' | sort | tr '\n' ' ')
fi

# Results land in files rather than on stdout, so that targets running at the
# same time do not interleave their lines. They are printed in target order
# once everything has finished, which keeps the output identical to a serial
# run whatever -j was.
RESULTS=$(mktemp -d)
trap 'rm -rf "$RESULTS"' EXIT INT TERM

run_one() {
    target=$1
    : > "$RESULTS/$target.out"
    echo 0 > "$RESULTS/$target.rc"
    bin=$BINDIR/$target
    if [ ! -x "$bin" ]; then
        echo "no such harness: $target" >&2
        echo 1 > "$RESULTS/$target.rc"
        return
    fi
    dir=$(corpus_dir_for "$target")
    mkdir -p "$dir"
    before=$(find "$dir" -type f | wc -l)

    if [ "$MINIMISE" -eq 1 ] && [ "$before" -gt 0 ]; then
        # -merge=1 keeps only the inputs that each add coverage. A corpus
        # grown over several sessions is mostly inputs that no longer do.
        min=$(mktemp -d "$CORPUS/min-XXXXXX")
        "$bin" -merge=1 "$min" "$dir" >/dev/null 2>&1
        if [ -n "$(ls -A "$min" 2>/dev/null)" ]; then
            rm -rf "$dir" && mv "$min" "$dir"
        else
            rm -rf "$min"
        fi
        before=$(find "$dir" -type f | wc -l)
    fi

    # libFuzzer takes its input-length limit from the largest file in the
    # corpus, and only defaults to 4096 when the corpus is empty. So seeding a
    # target with small files pins it below that default for good: fuzz_asn1
    # had been running on inputs of at most seven bytes, and fuzz_imagesize on
    # 184, against harnesses that accept a megabyte. Never lower what a corpus
    # has already reached; raise the floor to libFuzzer's own default, and
    # give the container parsers room for a structure worth parsing.
    largest=$(find "$dir" -type f -printf '%s\n' 2>/dev/null | sort -rn | head -1)
    largest=${largest:-0}
    # 4096 and no higher, on measurement rather than instinct: raising
    # fuzz_imagesize to 65536 grew its corpus but found no new edge in
    # forty-five seconds, while execution fell from about 717 a second to
    # 214. Bigger inputs are not free, and a target that needs them should
    # say so with evidence.
    floor=4096
    # Three harnesses cut their own input down before they look at it, so a
    # larger limit only has libFuzzer mutating bytes they throw away. Taken
    # from reading all twenty-two rather than from the three that were
    # noticed: every other one accepts 4096 or more, several far more.
    #
    #   fuzz_ringbuffer   size = 96     (and the slowest target there is)
    #   fuzz_rowdiff      size = 128
    #   fuzz_fileserver   size = 2048
    #
    # Each already held a corpus at about its own ceiling, so the limit they
    # inherited was the right one and the floor below would be a step back.
    case "$target" in
        fuzz_ringbuffer)  floor=128 ;;
        fuzz_rowdiff)     floor=160 ;;
        fuzz_fileserver)  floor=2048 ;;
    esac
    maxlen=$(( largest > floor ? largest : floor ))

    out=$("$bin" "$dir" -max_total_time="$SECS" -timeout=25 -max_len="$maxlen" \
          -print_final_stats=1 -artifact_prefix="$FINDINGS/$target-" 2>&1)
    rc=$?
    after=$(find "$dir" -type f | wc -l)
    cov=$(printf '%s' "$out" | grep -oE 'cov: [0-9]+' | tail -1 | cut -d' ' -f2)
    execs=$(printf '%s' "$out" | grep -oE 'stat::number_of_executed_units: *[0-9]+' \
            | tail -1 | grep -oE '[0-9]+$')

    # Executions per new input, because the raw count reads as effort and
    # says nothing about progress. Measured across one night, the ratio
    # spans 91,000 for fuzz_bootfiles to 321,403,755 for fuzz_asn1_length --
    # which spent its whole slot to learn one input and holds 20 edges. A
    # target up in the millions does not want more time; it wants a seed it
    # cannot invent, or an entry point nobody has given it. Three of this
    # tree's defects came from the latter and none from patience.
    gained=$(( after - before ))
    if [ "$gained" -gt 0 ] && [ -n "${execs:-}" ]; then
        per=$(( execs / gained ))
    else
        per="-"
    fi

    printf '%-24s corpus %5s -> %-5s cov %-6s execs %-11s per-new %s\n' \
        "$target" "$before" "$after" "${cov:-?}" "${execs:-?}" "$per" \
        >> "$RESULTS/$target.out"

    # UBSan runs with halt_on_error=0, because unsigned-integer-overflow
    # reports defined behaviour and halting on it would be wrong. That
    # setting is global, though: a genuine undefined-behaviour finding --
    # a signed overflow, a bad shift -- also prints and carries on, and
    # the target still exits 0. Everything below used to sit behind that
    # exit code, so those diagnostics went into the output and out again.
    # grep -c prints 0 *and* exits non-zero when it matches nothing, so a
    # `|| echo 0` fallback appends a second zero and the test below reads
    # "0\n0" as an integer. Let the assignment take grep's status instead.
    ubsan=$(printf '%s' "$out" | grep -cE 'runtime error:') || ubsan=0
    if [ "${ubsan:-0}" -gt 0 ]; then
        {
            echo "  UBSAN SAID SOMETHING ($ubsan lines, target exited $rc)"
            printf '%s\n' "$out" | grep -E 'runtime error:' \
                | sed 's/^.*runtime error: //' | sort -u | head -4 | sed 's/^/    /'
        } >> "$RESULTS/$target.out"
        echo 1 > "$RESULTS/$target.ub"
    fi

    if [ "$rc" -ne 0 ]; then
        {
            echo "  FOUND SOMETHING (exit $rc)"
            printf '%s\n' "$out" \
                | grep -E 'ERROR: (Address|Leak|UndefinedBehavior)Sanitizer|runtime error:|SUMMARY:|DEADLY SIGNAL|Test unit written' \
                | head -6 | sed 's/^/  /'
        } >> "$RESULTS/$target.out"
    fi
    echo "$rc" > "$RESULTS/$target.rc"
}

findings=0
started=0
for target in $TARGETS; do
    run_one "$target" &
    started=$((started + 1))
    if [ "$started" -ge "$JOBS" ]; then
        wait -n
        started=$((started - 1))
    fi
done
wait

ubtotal=0
for target in $TARGETS; do
    [ -f "$RESULTS/$target.out" ] && cat "$RESULTS/$target.out"
    rc=$(cat "$RESULTS/$target.rc" 2>/dev/null || echo 0)
    [ "$rc" -ne 0 ] && findings=$((findings + 1))
    [ -f "$RESULTS/$target.ub" ] && ubtotal=$((ubtotal + 1))
done
[ "$ubtotal" -gt 0 ] && echo "targets whose output carried a runtime error: $ubtotal"

echo "targets reporting something: $findings"
[ "$findings" -eq 0 ]
