#!/bin/sh
# Build binary .debs on the native host (no sbuild).
#
# Usage:
#   debian/build-binary-local.sh <arm64|amd64|armhf>
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: build-binary-local.sh <arch>}"

if [ "$ARCH" != "$HOST_ARCH" ]; then
	echo "build-binary-local: $ARCH != host $HOST_ARCH; use sbuild or build-appimages first" >&2
	exit 1
fi

ensure_dirs
APPIMAGE_DIR="$APPIMAGE_ROOT/$ARCH" APPIMAGE_DIR="$APPIMAGE_DIR" "$TOP/debian/stage-appimages.sh" all

_profiles=$(printf '%s' "$DEB_BUILD_PROFILES" | tr ' ' ',')

(
	cd "$TOP"
	# shellcheck disable=SC2086
	dpkg-buildpackage -b -uc -us -a"$ARCH" -P"$_profiles"
)

for _deb in ../*.deb; do
	[ -f "$_deb" ] || continue
	mv "$_deb" "$OUTPUT_DIR/"
done
for _changes in ../*_"$ARCH".buildinfo ../*_"$ARCH".changes; do
	[ -f "$_changes" ] || continue
	mv "$_changes" "$OUTPUT_DIR/" 2>/dev/null || true
done

echo "build-binary-local: wrote packages to $OUTPUT_DIR"
