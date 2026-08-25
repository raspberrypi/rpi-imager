#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2025 Raspberry Pi Ltd
#
# Shared plumbing for the embedded display-scaling harness: synthesising a
# /sys/class/drm tree and running something against it. Sourced by run.sh
# (which checks the scale factor chosen) and screenshots.sh (which renders the
# UI at that factor). Not executable on its own.

lib_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
mkedid=$lib_dir/mkedid.sh

# Refuse to run where the harness cannot bind a directory over /sys/class/drm,
# which needs an unprivileged mount namespace. Callers exit 4 on failure so
# CTest records a skip rather than a failure.
scaling_preflight() {
    if ! unshare -rm --propagation private true 2>/dev/null; then
        echo "SKIP: unprivileged mount namespaces are unavailable (unshare -rm failed)" >&2
        return 1
    fi
    # shellcheck disable=SC2016 # $1 is for the inner shell, not this one
    if ! unshare -rm --propagation private \
            sh -c 'mount --bind "$1" /sys/class/drm' _ "$lib_dir" 2>/dev/null; then
        echo "SKIP: cannot bind-mount over /sys/class/drm in a mount namespace" >&2
        return 1
    fi
    return 0
}

# Translate one comma-separated EDID recipe into mkedid.sh arguments,
# e.g. "px=3840x2160,mm=951x535,extensions=1".
scaling_edid_args() {
    local recipe=$1 field
    local -a args=()
    for field in ${recipe//,/ }; do
        case $field in
            px=*|mm=*|cm=*|extensions=*|truncate=*)
                args+=("--${field%%=*}" "${field#*=}") ;;
            no-dtd-size|monitor-descriptor|bad-header)
                args+=("--$field") ;;
            *)
                echo "unknown EDID recipe field '$field'" >&2
                return 2 ;;
        esac
    done
    printf '%s\n' "${args[@]}"
}

# Build one fake /sys/class/drm. The display spec is ';'-separated connectors,
# each written as name|status|modes|edid; see cases.txt for the field rules.
scaling_build_fixture() {
    local dir=$1 displays=$2

    # Real trees carry the card device, its render node and a version file
    # alongside the connectors, so every fixture carries them too -- that way
    # each case also exercises the connector glob ignoring them.
    mkdir -p "$dir/card1" "$dir/renderD128"
    printf 'drm 1.1.0 20060810\n' > "$dir/version"
    [ "$displays" = "-" ] && return 0

    local saved_ifs=$IFS
    IFS=';'
    # shellcheck disable=SC2086 # deliberate split on ';'
    set -- $displays
    IFS=$saved_ifs

    local display name status modes edid
    for display in "$@"; do
        IFS='|' read -r name status modes edid <<<"$display"
        mkdir -p "$dir/$name"
        [ "$status" = "-" ] || printf '%s\n' "$status" > "$dir/$name/status"
        case $modes in
            -)     ;;
            empty) : > "$dir/$name/modes" ;;
            *)     printf '%s\n' "${modes//,/$'\n'}" > "$dir/$name/modes" ;;
        esac
        case $edid in
            -)     ;;
            empty) : > "$dir/$name/edid" ;;
            *)     local -a args
                   mapfile -t args < <(scaling_edid_args "$edid") || return 2
                   "$mkedid" "${args[@]}" > "$dir/$name/edid" || return 2 ;;
        esac
    done
}

# Run a command with the given fixture bind-mounted over /sys/class/drm, so the
# code under test reads our synthetic display instead of the host's.
scaling_run_in_fixture() {
    local fixture=$1
    shift
    # shellcheck disable=SC2016 # $1 is for the inner shell, not this one
    unshare -rm --propagation private \
        sh -c 'mount --bind "$1" /sys/class/drm && shift && exec "$@"' _ "$fixture" "$@"
}
