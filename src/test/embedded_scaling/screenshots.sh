#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2025 Raspberry Pi Ltd
#
# Render the embedded imager UI at each display profile and save a PNG, so a
# scale factor can be judged by looking at the interface rather than trusting
# the number.
#
# Same mechanism as run.sh: a synthetic /sys/class/drm is bind-mounted over the
# real one so the app's own scaling code reads the panel we want it to see. The
# UI is then rendered onto an offscreen screen of that panel's resolution, and
# the app's screenshot hook (RPI_IMAGER_SCREENSHOT) writes the frame out.
#
# Needs a GUI build of the imager configured with -DBUILD_EMBEDDED=ON, so the
# embedded code paths and layout are the ones exercised, and -DENABLE_TEST_HOOKS=ON,
# which compiles in the screenshot hook this relies on. Release builds leave that
# hook out on purpose: it writes a capture of the window to a caller-chosen path,
# in a binary the embedded image runs as root.
#
# Usage: screenshots.sh /path/to/rpi-imager [output-dir]
#
#   RPI_SCALING_PROFILES=<file>  profile list (default: profiles.txt beside this)
#   RPI_SCALING_FILTER=<glob>    render only profiles whose name matches
#   RPI_SCALING_REPO=<url|file>  OS list to use, for a deterministic first screen
#   RPI_SCALING_STEP=<step>      jump the wizard to this step before grabbing, naming
#                                the output <profile>.<step>.png. Either a step index
#                                or a WizardContainer constant without its prefix,
#                                e.g. WifiCustomization or IfAndFeatures. Embedded
#                                mode always opens on language selection, so this is
#                                how a form-heavy page gets judged.
#   RPI_SCALING_FORCE_SCALE=<n>  render every profile at this QT_SCALE_FACTOR instead
#                                of the one the scaling code picks, and name the
#                                output <profile>@<n>x.png. For judging what a
#                                panel should get: the app respects a pre-set
#                                factor, so this drives the real override path.
#   RPI_SCALING_DELAY_MS=<ms>    settle time before the grab (default 3000)
#   RPI_SCALING_TIMEOUT=<s>      per-profile timeout (default 90)
#   RPI_SCALING_KEEP=1           keep fixtures, screen configs and app logs
#
# Exits 0 when every profile rendered at its expected factor, 1 otherwise, and
# 4 where the host cannot provide an unprivileged mount namespace.
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=lib.sh
. "$here/lib.sh"

profiles=${RPI_SCALING_PROFILES:-$here/profiles.txt}
force_scale=${RPI_SCALING_FORCE_SCALE:-}
step=${RPI_SCALING_STEP:-}
filter=${RPI_SCALING_FILTER:-*}
delay_ms=${RPI_SCALING_DELAY_MS:-3000}
timeout_s=${RPI_SCALING_TIMEOUT:-90}

imager=${1:-}
outdir=${2:-$PWD/screenshots}
if [ -z "$imager" ]; then
    echo "usage: $(basename "$0") /path/to/rpi-imager [output-dir]" >&2
    exit 2
fi
if [ ! -x "$imager" ]; then
    echo "$(basename "$0"): '$imager' is not an executable imager build" >&2
    exit 2
fi
imager=$(cd "$(dirname "$imager")" && pwd)/$(basename "$imager")
[ -r "$profiles" ] || { echo "$(basename "$0"): cannot read profile list '$profiles'" >&2; exit 2; }

scaling_preflight || exit 4

work=$(mktemp -d "${TMPDIR:-/tmp}/rpi-scaling-shots.XXXXXX")
cleanup() {
    if [ -n "${RPI_SCALING_KEEP:-}" ]; then
        echo "fixtures, screen configs and app logs kept in $work"
    else
        rm -rf "$work"
    fi
}
trap cleanup EXIT
mkdir -p "$outdir"

# A screen for Qt's offscreen platform plugin, which takes its geometry and DPI
# from a JSON file rather than from any real display.
write_screen_config() {
    local path=$1 width=$2 height=$3 dpi=$4
    cat > "$path" <<JSON
{
    "screens": [
        {
            "name": "HDMI-A-1",
            "x": 0,
            "y": 0,
            "width": $width,
            "height": $height,
            "logicalDpi": $dpi,
            "logicalBaseDpi": 96
        }
    ]
}
JSON
}

# A build without the screenshot hook never acts on RPI_IMAGER_SCREENSHOT, so it
# runs until the watchdog kills it. Say so rather than leaving a bare timeout.
hook_missing() {
    grep -q "Screenshot:" "$1" && return 0
    printf '        the build never acted on RPI_IMAGER_SCREENSHOT. Release builds compile\n'
    printf '        the hook out; configure with -DENABLE_TEST_HOOKS=ON to include it.\n'
}

printf 'embedded UI screenshots: %s -> %s\n\n' "$(basename "$profiles")" "$outdir"

rendered=0 failed=0 skipped=0
failures=()
while read -r name resolution edid dpi expected extra; do
    case $name in ''|\#*) continue ;; esac
    if [ -z "${expected:-}" ] || [ -n "${extra:-}" ]; then
        echo "  ERROR $name: expected 5 columns in the profile list" >&2
        failed=$((failed + 1))
        continue
    fi
    # shellcheck disable=SC2254 # the filter is deliberately a glob
    case $name in $filter) ;; *) skipped=$((skipped + 1)); continue ;; esac

    width=${resolution%%x*}
    height=${resolution##*x}

    # The rendering screen must contribute no scaling of its own. On the device
    # the device pixel ratio comes from QT_SCALE_FACTOR alone, whereas Qt's
    # offscreen plugin derives one from logicalDpi/logicalBaseDpi -- leave the
    # two equal and the factor chosen by the code under test is the only thing
    # scaling the UI. A profile can still name a DPI to deviate deliberately.
    [ "$dpi" = auto ] && dpi=96

    # A forced factor replaces both what we run at and what we expect, so a
    # sweep over candidate factors reports honestly rather than failing every row.
    label=$name
    if [ -n "$force_scale" ]; then
        expected=$force_scale
        label="$name@${force_scale}x"
    fi
    [ -z "$step" ] || label="$label.$step"

    fixture=$work/$label
    config=$work/$label.screen.json
    log=$work/$label.log
    png=$outdir/$label.png

    if ! scaling_build_fixture "$fixture" "card1-HDMI-A-1|connected|$resolution|$edid" 2>>"$log"; then
        echo "  ERROR $name: could not build the fixture" >&2
        sed 's/^/         /' "$log" >&2
        failed=$((failed + 1))
        continue
    fi
    write_screen_config "$config" "$width" "$height" "$dpi"

    # A private HOME keeps the app's QSettings (saved language, window
    # position) out of the caller's real profile and identical run to run.
    app_home=$work/$name.home
    mkdir -p "$app_home"

    declare -a app_env=(env)
    if [ -n "$force_scale" ]; then
        app_env+=("QT_SCALE_FACTOR=$force_scale")
    else
        app_env+=(-u QT_SCALE_FACTOR)
    fi
    app_env+=(
        "HOME=$app_home"
        "XDG_CONFIG_HOME=$app_home/config"
        "XDG_CACHE_HOME=$app_home/cache"
        "XDG_DATA_HOME=$app_home/data"
        "QT_QPA_PLATFORM=offscreen:configfile=$config"
        "QT_QUICK_BACKEND=software"
        "QSG_RENDER_LOOP=basic"
        "QT_QUICK_DEFAULT_TEXT_RENDER_TYPE=NativeRendering"
        "RPI_IMAGER_SCREENSHOT=$png"
        "RPI_IMAGER_SCREENSHOT_DELAY_MS=$delay_ms"
    )
    [ -z "$step" ] || app_env+=("RPI_IMAGER_SCREENSHOT_STEP=$step")
    declare -a app_args=("$imager")
    [ -n "${RPI_SCALING_REPO:-}" ] && app_args+=(--repo "$RPI_SCALING_REPO")

    rm -f "$png"
    scaling_run_in_fixture "$fixture" timeout "$timeout_s" \
        "${app_env[@]}" "${app_args[@]}" >>"$log" 2>&1
    status=$?

    chosen=$(sed -n 's/.*Embedded scaling:.*QT_SCALE_FACTOR=\([0-9.]*\).*/\1/p' "$log" | tail -1)
    # A factor we forced is reported by the override branch instead, which says
    # it is leaving an already-set value alone.
    [ -n "$chosen" ] || chosen=$(sed -n 's/.*QT_SCALE_FACTOR already set to "\([0-9.]*\)".*/\1/p' "$log" | tail -1)
    [ -n "$chosen" ] || chosen="unset"
    grabbed=$(sed -n 's/.*Screenshot: wrote .* at \([0-9]*x[0-9]*\) px.*/\1/p' "$log" | tail -1)

    if [ $status -eq 124 ]; then
        printf '  ERROR %-20s timed out after %ss\n' "$label" "$timeout_s"
        hook_missing "$log"
        failed=$((failed + 1))
        continue
    fi
    if [ ! -s "$png" ]; then
        printf '  ERROR %-20s no screenshot written (exit %d)\n' "$label" "$status"
        hook_missing "$log"
        tail -20 "$log" | sed 's/^/         /'
        failed=$((failed + 1))
        continue
    fi
    if [ "$chosen" != "$expected" ]; then
        printf '  FAIL  %-20s rendered at scale %s, expected %s (%s)\n' \
            "$label" "$chosen" "$expected" "$grabbed"
        failures+=("$label")
        failed=$((failed + 1))
        continue
    fi
    if [ "$grabbed" != "${width}x${height}" ]; then
        printf '  FAIL  %-20s scale %s but grabbed %s, expected %sx%s\n' \
            "$label" "$chosen" "$grabbed" "$width" "$height"
        failures+=("$label")
        failed=$((failed + 1))
        continue
    fi

    printf '  ok    %-24s %s @ %s DPI, scale %s -> %s\n' \
        "$label" "$grabbed" "$dpi" "$chosen" "$png"
    rendered=$((rendered + 1))
done < "$profiles"

printf '\n%d rendered, %d failed' "$rendered" "$failed"
[ "$skipped" -gt 0 ] && printf ', %d not matching RPI_SCALING_FILTER' "$skipped"
printf '\n'

if [ "$failed" -gt 0 ]; then
    [ ${#failures[@]} -gt 0 ] && printf 'failing profiles: %s\n' "${failures[*]}"
    exit 1
fi
exit 0
