#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Usage: prune_unlinked_libs.sh <app bundle>

# macdeployqt copies the libraries behind every QML plugin it finds; the
# plugins pruned after it leave those behind. Remove each Frameworks dylib
# nothing left in the bundle loads, until none goes.
set -eu
app=$1
fw="$app/Contents/Frameworks"

load_commands() {
	otool -l "$1" 2>/dev/null | awk '
		/cmd LC_(LOAD|LOAD_WEAK|REEXPORT|LAZY_LOAD|LOAD_UPWARD)_DYLIB$/ { want = 1; next }
		want && $1 == "name" { n = $2; sub(/.*\//, "", n); print n; want = 0 }'
}

while :; do
	used=$(find "$app/Contents/MacOS" "$app/Contents/PlugIns" "$fw" -type f |
		while IFS= read -r f; do load_commands "$f"; done | sort -u)
	removed=0
	for lib in "$fw"/*.dylib; do
		[ -f "$lib" ] || continue
		name=${lib##*/}
		if ! printf '%s\n' "$used" | grep -qxF "$name"; then
			echo "Removing unlinked $name"
			rm -f "$lib"
			removed=1
		fi
	done
	[ "$removed" -eq 1 ] || break
done
