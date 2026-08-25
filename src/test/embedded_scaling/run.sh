#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2025 Raspberry Pi Ltd
#
# Display-scaling matrix for the embedded (linuxfb) imager.
#
# PlatformQuirks::applyEmbeddedDisplayScaling() picks the UI scale by reading
# the connected display straight out of /sys/class/drm, so on a build host it
# only ever sees that host's display -- usually none at all. This runner feeds
# the real, unmodified code synthetic sysfs trees instead: for each case in
# cases.txt it builds a directory of fake connectors, bind-mounts it over
# /sys/class/drm inside an unprivileged mount namespace, and checks the factor
# the code then chooses (reported by embedded_scaling_probe).
#
# See screenshots.sh to render the UI at those factors rather than only check
# which factor was picked.
#
# Usage: run.sh /path/to/embedded_scaling_probe
#
#   RPI_SCALING_CASES=<file>    case list to run (default: cases.txt beside this script)
#   RPI_SCALING_FILTER=<glob>   run only cases whose name matches
#   RPI_SCALING_KEEP=1          keep the fixture trees and probe logs
#
# Exits 0 when every case matches, 1 on any mismatch, 4 when the host cannot
# provide an unprivileged mount namespace (CTest reads 4 as "skipped").
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=lib.sh
. "$here/lib.sh"

cases=${RPI_SCALING_CASES:-$here/cases.txt}
filter=${RPI_SCALING_FILTER:-*}

probe=${1:-}
if [ -z "$probe" ]; then
    echo "usage: $(basename "$0") /path/to/embedded_scaling_probe" >&2
    exit 2
fi
if [ ! -x "$probe" ]; then
    echo "$(basename "$0"): '$probe' is not an executable probe" >&2
    exit 2
fi
probe=$(cd "$(dirname "$probe")" && pwd)/$(basename "$probe")
[ -r "$cases" ] || { echo "$(basename "$0"): cannot read case list '$cases'" >&2; exit 2; }

scaling_preflight || exit 4

work=$(mktemp -d "${TMPDIR:-/tmp}/rpi-scaling-matrix.XXXXXX")
cleanup() {
    if [ -n "${RPI_SCALING_KEEP:-}" ]; then
        echo "fixtures and logs kept in $work"
    else
        rm -rf "$work"
    fi
}
trap cleanup EXIT

# QT_SCALE_FACTOR is cleared unless a case sets it, so a stray value in the
# caller's environment cannot make the override path pass by accident.
run_probe() {
    local fixture=$1 case_env=$2
    local -a env_args=(env -u QT_SCALE_FACTOR)
    [ "$case_env" = "-" ] || env_args+=("$case_env")
    scaling_run_in_fixture "$fixture" "${env_args[@]}" "$probe"
}

printf 'embedded display scaling matrix: %s\n\n' "$(basename "$cases")"

passed=0 failed=0 skipped=0
failures=()
while read -r name expected case_env displays extra; do
    case $name in ''|\#*) continue ;; esac
    if [ -z "${displays:-}" ] || [ -n "${extra:-}" ]; then
        echo "  ERROR $name: expected 4 columns in the case list" >&2
        failed=$((failed + 1))
        continue
    fi
    # shellcheck disable=SC2254 # the filter is deliberately a glob
    case $name in $filter) ;; *) skipped=$((skipped + 1)); continue ;; esac

    fixture=$work/$name
    log=$work/$name.log
    if ! scaling_build_fixture "$fixture" "$displays" 2>>"$log"; then
        echo "  ERROR $name: could not build the fixture" >&2
        sed 's/^/         /' "$log" >&2
        failed=$((failed + 1))
        continue
    fi

    actual=$(run_probe "$fixture" "$case_env" 2>>"$log")
    status=$?
    if [ $status -ne 0 ]; then
        printf '  ERROR %-32s probe exited %d\n' "$name" "$status"
        sed 's/^/         /' "$log"
        failed=$((failed + 1))
        continue
    fi

    if [ "$actual" = "$expected" ]; then
        printf '  ok    %-32s %s\n' "$name" "$actual"
        passed=$((passed + 1))
    else
        printf '  FAIL  %-32s %s (expected %s)\n' "$name" "$actual" "$expected"
        sed 's/^/         /' "$log"
        failures+=("$name")
        failed=$((failed + 1))
    fi
done < "$cases"

printf '\n%d passed, %d failed' "$passed" "$failed"
[ "$skipped" -gt 0 ] && printf ', %d not matching RPI_SCALING_FILTER' "$skipped"
printf '\n'

if [ "$failed" -gt 0 ]; then
    [ ${#failures[@]} -gt 0 ] && printf 'failing cases: %s\n' "${failures[*]}"
    exit 1
fi
exit 0
