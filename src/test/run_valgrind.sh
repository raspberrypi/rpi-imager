#!/bin/bash
# Memcheck over the parsers, for the one thing the other checks cannot see.
#
# AddressSanitizer finds what is written out of bounds and what is never
# freed. It does not find a *read of memory nobody wrote* -- that is
# MemorySanitizer's job, and MemorySanitizer needs every library beneath it
# instrumented, Qt included. There is no instrumented Qt on this machine,
# which is the same wall TSan hit. Valgrind needs no instrumentation at all,
# so it is the only way to ask that question here.
#
# It costs twenty to fifty times the runtime, so this is deliberately not the
# whole suite: it is the binaries that parse something arriving from outside
# the program, where a field nobody filled in is a figure the writer then
# acts on.
#
# Not the QML storms, and that is measured rather than assumed. One storm
# under Memcheck reports twenty uninitialised-value reads, and every one of
# them is inside libpcre2-16 (twelve) or libQt6Gui (eight) -- PCRE2's matcher
# reads uninitialised bytes on purpose, which Memcheck cannot know. Nothing
# came from our code. Suppressing both wholesale would leave a check that
# could no longer see the paths worth checking, so the storms are left to the
# sanitiser builds and this stays with the parsers.
#
# Usage: run_valgrind.sh [-b build] [target ...]
# Exits 0 when every target is clean.

set -u

BUILD=build-uitest
while getopts "b:h" opt; do
    case $opt in
        b) BUILD=$OPTARG ;;
        h) sed -n '2,18p' "$0"; exit 0 ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))

HERE=$(cd "$(dirname "$0")" && pwd)
BINDIR=$BUILD/test

# The parsers, and the hash both sides of the write comparison are taken with.
# fat_partition_image_test earns its place: it is the FAT driver's only real
# coverage -- fat_partition_test skips all four of its cases without a mounted
# device -- and the first Valgrind run over it reported a branch on an
# uninitialised value 380 times. ASan cannot see that class and MSan needs an
# instrumented Qt, which is gone, so this runner is the only thing that would
# have found it. sparse_encoder_test covers the unaligned-read fix, and
# pieeprom_test a parser the bootloader's own flash feeds.
DEFAULT_TARGETS="bmap_test image_size_parser_test oslist_parser_test
                 config_txt_merge_test block_batcher_test
                 accelerated_hash_test bootloader_image_test
                 fat_partition_image_test sparse_encoder_test pieeprom_test"

TARGETS=${*:-$DEFAULT_TARGETS}

if ! command -v valgrind >/dev/null; then
    echo "valgrind is not installed" >&2
    exit 2
fi

failures=0
for target in $TARGETS; do
    bin=$BINDIR/$target
    if [ ! -x "$bin" ]; then
        echo "no such binary: $bin" >&2
        failures=$((failures + 1))
        continue
    fi

    out=$(valgrind --tool=memcheck \
                   --track-origins=yes \
                   --leak-check=no \
                   --suppressions="$HERE/valgrind-qt.supp" \
                   "$bin" 2>&1)
    errs=$(printf '%s' "$out" | grep -oE 'ERROR SUMMARY: [0-9]+' | tail -1 \
           | grep -oE '[0-9]+$')
    tests=$(printf '%s' "$out" | grep -oE 'All tests passed \([0-9]+ assertions[^)]*\)' | tail -1)

    printf '%-28s errors %-4s %s\n' "$target" "${errs:-?}" "${tests:-TESTS DID NOT PASS}"

    if [ "${errs:-1}" -ne 0 ]; then
        failures=$((failures + 1))
        printf '%s\n' "$out" \
            | grep -E 'Conditional jump|uninitialised|Invalid read|Invalid write|Syscall param|^==[0-9]+==    (at|by) ' \
            | head -12 | sed 's/^/  /'
    fi
done

echo "targets with something to look at: $failures"
[ "$failures" -eq 0 ]
