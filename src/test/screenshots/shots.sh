#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Photograph the desktop imager UI, one PNG per wizard page, so the screenshots
# the AppStream metainfo and the store listings point at can be regenerated
# from the current build instead of aging in place.
#
# The frames are of the real application. Three things are substituted, and
# only so that a run on one machine matches a run on another: a stand-in lsblk
# on PATH, so the storage page never shows the build host's real disks; Qt's
# offscreen platform, so no X or Wayland session is needed; and an unprivileged
# user namespace, without which every page renders under the modal permissions
# dialog. The writing and completion pages come from a real write to a sparse
# file standing in for the card -- see the RPI_IMAGER_SCREENSHOT_WRITE hook in
# src/main.cpp. Nothing here touches a block device. README.md has the detail.
#
# Needs a desktop GUI build configured with -DENABLE_TEST_HOOKS=ON, which
# compiles in the screenshot hook. Release builds leave it out on purpose.
#
# Usage: shots.sh /path/to/rpi-imager [output-dir]
#
#   RPI_SHOT_PAGES=<file>      page list (default: pages.txt beside this)
#   RPI_SHOT_FILTER=<glob>     render only pages whose name matches
#   RPI_SHOT_SCALE=<n>         QT_SCALE_FACTOR, so the 680x450 window is
#                              published at n times that (default 2)
#   RPI_SHOT_REPO=<url|file>   OS list to render (default: the shipped one,
#                              fetched once into the cache)
#   RPI_SHOT_DELAY_MS=<ms>     settle time before the page is opened (default 6000)
#   RPI_SHOT_WRITE_AT=<pct>    how far through the write to photograph it (default 45)
#   RPI_SHOT_IMAGE_SIZE=<n>    source image to write, in numfmt(1) IEC units (default 512M)
#   RPI_SHOT_CACHE=<dir>       where the OS list and source image are kept
#                              between runs (default: <output-dir>/.cache)
#   RPI_SHOT_TIMEOUT=<s>       per-page timeout (default 300)
#   RPI_SHOT_KEEP=1            keep fixtures and app logs
#
# Exits 0 when every page rendered, 1 otherwise, and 4 where the host can offer
# neither root nor an unprivileged user namespace.
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)

pages=${RPI_SHOT_PAGES:-$here/pages.txt}
filter=${RPI_SHOT_FILTER:-*}
scale=${RPI_SHOT_SCALE:-2}
delay_ms=${RPI_SHOT_DELAY_MS:-6000}
write_at=${RPI_SHOT_WRITE_AT:-45}
image_size=${RPI_SHOT_IMAGE_SIZE:-512M}
timeout_s=${RPI_SHOT_TIMEOUT:-300}

# The card the storage page offers and the write is aimed at. The stand-in
# lsblk below and the write's own dst= are generated from these, so the page
# and the write cannot drift apart.
card_size=32017047552
card_name="Generic USB SD Reader"
shown_device="Raspberry Pi 5"
shown_os="Raspberry Pi OS (64-bit)"

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
[ -r "$pages" ] || { echo "$(basename "$0"): cannot read page list '$pages'" >&2; exit 2; }

mkdir -p "$outdir"
outdir=$(cd "$outdir" && pwd)
cache=${RPI_SHOT_CACHE:-$outdir/.cache}
mkdir -p "$cache"

# Root already satisfies the elevation check; everyone else borrows it from a
# user namespace, which needs no privilege on the host and is the same
# mechanism src/test/embedded_scaling relies on.
elevate=()
if [ "$(id -u)" != 0 ]; then
    if unshare -r true 2>/dev/null; then
        elevate=(unshare -r --)
    else
        echo "$(basename "$0"): needs root or an unprivileged user namespace;" \
             "without either, every page renders under the permissions dialog" >&2
        exit 4
    fi
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/rpi-shots.XXXXXX")
cleanup() {
    if [ -n "${RPI_SHOT_KEEP:-}" ]; then
        echo "fixtures and app logs kept in $work"
    else
        rm -rf "$work"
    fi
}
trap cleanup EXIT

# A stand-in lsblk. drivelist_linux.cpp starts "lsblk" through PATH and reads
# nothing but its JSON, so this is the whole of the substitution.
mkdir -p "$work/bin"
cat > "$work/bin/lsblk" <<LSBLK
#!/bin/sh
cat <<'JSON'
{
  "blockdevices": [
    {"kname":"/dev/nvme0n1","type":"disk","subsystems":"block:nvme:pci","ro":false,"rm":false,
     "hotplug":false,"size":1000204886016,"phy-sec":512,"log-sec":512,
     "label":null,"vendor":null,"model":"Samsung SSD 990 PRO 1TB","mountpoint":null},
    {"kname":"/dev/sda","type":"disk","subsystems":"block:scsi:usb","ro":false,"rm":true,
     "hotplug":true,"size":$card_size,"phy-sec":512,"log-sec":512,
     "label":null,"vendor":"Generic","model":"USB SD Reader","mountpoint":null},
    {"kname":"/dev/sdb","type":"disk","subsystems":"block:scsi:usb","ro":false,"rm":true,
     "hotplug":true,"size":128035676160,"phy-sec":512,"log-sec":512,
     "label":null,"vendor":"SanDisk","model":"Extreme Pro","mountpoint":null}
  ]
}
JSON
LSBLK
chmod +x "$work/bin/lsblk"

# A screen for Qt's offscreen platform, which takes its geometry from a JSON
# file rather than from any real display. Larger than the window needs, and
# with logicalDpi equal to logicalBaseDpi, so the requested scale factor is the
# only thing scaling the UI.
cat > "$work/screen.json" <<JSON
{
    "screens": [
        {
            "name": "HDMI-A-1",
            "x": 0,
            "y": 0,
            "width": 3840,
            "height": 2160,
            "logicalDpi": 96,
            "logicalBaseDpi": 96
        }
    ]
}
JSON

# The OS list. Pinning a downloaded copy is what makes two runs weeks apart
# comparable; without one the pages would differ whenever the list changes.
repo=${RPI_SHOT_REPO:-}
if [ -z "$repo" ]; then
    repo=$cache/os_list.json
    if [ ! -s "$repo" ]; then
        url=$(sed -n 's/^#define OSLIST_URL *"\(.*\)".*/\1/p' "$here/../../config.h")
        echo "fetching the OS list into $repo"
        if ! curl -fsS -o "$repo" "$url"; then
            echo "$(basename "$0"): could not fetch the OS list from $url" >&2
            rm -f "$repo"
            exit 1
        fi
    fi
fi

# A source image only the write pages need, so it is built on first use and
# kept. Incompressible, so the write moves real bytes and the progress the
# page reports is the progress of a real copy.
ensure_write_fixtures() {
    write_src=$cache/source.img
    if [ ! -s "$write_src" ]; then
        echo "building a $image_size source image in $write_src"
        # Built under a temporary name, so an interrupted run leaves no short
        # image behind for the next one to accept and write.
        head -c "$(numfmt --from=iec "$image_size")" /dev/urandom > "$write_src.part" \
            || { rm -f "$write_src.part"; return 1; }
        mv "$write_src.part" "$write_src"
    fi
    # The card: sparse, so it costs only what the write puts in it.
    write_dst=$work/card.img
    truncate -s "$card_size" "$write_dst"
}

printf 'desktop UI screenshots: %s -> %s\n\n' "$(basename "$pages")" "$outdir"

rendered=0 failed=0 skipped=0
failures=()
while read -r page mode published extra; do
    case $page in ''|\#*) continue ;; esac
    if [ -z "${published:-}" ] || [ -n "${extra:-}" ]; then
        echo "  ERROR $page: expected 3 columns in the page list" >&2
        failed=$((failed + 1))
        continue
    fi
    # shellcheck disable=SC2254 # the filter is deliberately a glob
    case $page in $filter) ;; *) skipped=$((skipped + 1)); continue ;; esac

    log=$work/$page.log
    png=$outdir/$page.png

    declare -a app_env=(
        "QT_QPA_PLATFORM=offscreen:configfile=$work/screen.json"
        "QT_QUICK_BACKEND=software"
        "QSG_RENDER_LOOP=basic"
        "QT_QUICK_DEFAULT_TEXT_RENDER_TYPE=NativeRendering"
        "QT_SCALE_FACTOR=$scale"
        "RPI_IMAGER_SCREENSHOT=$png"
        "RPI_IMAGER_SCREENSHOT_DELAY_MS=$delay_ms"
        "RPI_IMAGER_SCREENSHOT_STEP=$page"
    )
    if [ "$mode" = write ]; then
        write_timeout_ms=$(( timeout_s * 1000 - 10000 ))
        [ "$write_timeout_ms" -lt 10000 ] && write_timeout_ms=10000
        if ! ensure_write_fixtures; then
            echo "  ERROR $page: could not build the write fixtures" >&2
            failed=$((failed + 1))
            continue
        fi
        app_env+=(
            "RPI_IMAGER_SCREENSHOT_WRITE=src=$write_src,dst=$write_dst,size=$card_size,device=$shown_device,os=$shown_os,storage=$card_name"
            "RPI_IMAGER_SCREENSHOT_WRITE_AT=$write_at"
            # Just inside the shell's own timeout, so a write that never
            # reaches the threshold is reported as that rather than as a
            # process killed for taking too long. Floored, because a
            # RPI_SHOT_TIMEOUT under ten seconds would otherwise hand the hook
            # a negative deadline and it would give up before the first byte.
            "RPI_IMAGER_SCREENSHOT_WRITE_TIMEOUT_MS=$write_timeout_ms"
        )
    elif [ "$mode" != jump ]; then
        echo "  ERROR $page: mode must be jump or write, not '$mode'" >&2
        failed=$((failed + 1))
        continue
    fi

    # A private HOME keeps the app's QSettings out of the caller's real profile
    # and identical run to run, and pins the text scale so the layout is the
    # scale factor's doing alone.
    app_home=$work/$page.home
    mkdir -p "$app_home/config/Raspberry Pi"
    printf '[General]\ntextScaleFactor=1\n' \
        > "$app_home/config/Raspberry Pi/Raspberry Pi Imager.conf"
    app_env+=(
        "HOME=$app_home"
        "XDG_CONFIG_HOME=$app_home/config"
        "XDG_CACHE_HOME=$app_home/cache"
        "XDG_DATA_HOME=$app_home/data"
    )

    rm -f "$png"
    PATH="$work/bin:$PATH" timeout "$timeout_s" "${elevate[@]}" \
        env "${app_env[@]}" "$imager" --repo "$repo" >"$log" 2>&1
    status=$?

    grabbed=$(sed -n 's/.*Screenshot: wrote .* at \([0-9]*x[0-9]*\) px.*/\1/p' "$log" | tail -1)

    if [ ! -s "$png" ]; then
        if [ $status -eq 124 ]; then
            printf '  ERROR %-24s timed out after %ss\n' "$page" "$timeout_s"
        else
            printf '  ERROR %-24s no screenshot written (exit %d)\n' "$page" "$status"
        fi
        # A build without the hook never acts on RPI_IMAGER_SCREENSHOT and so
        # runs until the watchdog kills it: only a timeout points that way.
        # Any other exit is the app's own -- a missing library, a bad repo
        # file -- and blaming the hook for those sends the reader after the
        # wrong thing, so the log gets the last word either way.
        if [ $status -eq 124 ] && ! grep -q "Screenshot:" "$log"; then
            printf '        the build never acted on RPI_IMAGER_SCREENSHOT. Release builds\n'
            printf '        compile the hook out; configure with -DENABLE_TEST_HOOKS=ON.\n'
        fi
        tail -10 "$log" | sed 's/^/          /'
        failures+=("$page")
        failed=$((failed + 1))
        continue
    fi

    note=""
    if [ "$published" != "-" ]; then
        mkdir -p "$outdir/metainfo"
        cp "$png" "$outdir/metainfo/$published.png"
        note=" -> metainfo/$published.png"
    fi
    printf '  ok    %-24s %s%s\n' "$page" "$grabbed" "$note"
    rendered=$((rendered + 1))
done < "$pages"

printf '\n%d rendered, %d failed' "$rendered" "$failed"
[ "$skipped" -gt 0 ] && printf ', %d not matching RPI_SHOT_FILTER' "$skipped"
printf '\n'

if [ "$failed" -gt 0 ]; then
    [ ${#failures[@]} -gt 0 ] && printf 'failing pages: %s\n' "${failures[*]}"
    exit 1
fi
exit 0
