#!/bin/sh
# Sync locally built AppImages into the configured per-arch cache.
#
# Usage:
#   debian/sync-appimages.sh <arm64|amd64|armhf>
#
# SRC_DIR defaults to the repository root. Override APPIMAGE_ROOT in
# debian/release.conf.
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: sync-appimages.sh <arm64|amd64|armhf>}"
SRC_DIR=${SRC_DIR:-$TOP}
DEST="$APPIMAGE_ROOT/$ARCH"

pick_newest() {
	pattern=$1
	# shellcheck disable=SC2086
	ls -1t $SRC_DIR/$pattern 2>/dev/null | head -1
}

IMG_ARCH=$(deb_to_image_arch "$ARCH")
ensure_dirs
install -d "$DEST"

copy_one() {
	label=$1
	src=$2
	dest_name=$3

	if [ -z "$src" ] || [ ! -e "$src" ]; then
		echo "sync-appimages: missing $label for $ARCH (tried $SRC_DIR)" >&2
		return 1
	fi

	cp -f "$src" "$DEST/$(basename "$src")"
	ln -sfn "$(basename "$src")" "$DEST/$dest_name"
	echo "sync-appimages: $dest_name -> $DEST/$(basename "$src")"
}

DESKTOP=$(pick_newest "Raspberry_Pi_Imager-*-desktop-${IMG_ARCH}.AppImage")
CLI=$(pick_newest "Raspberry_Pi_Imager-*-cli-${IMG_ARCH}.AppImage")

copy_one desktop "$DESKTOP" "rpi-imager-${IMG_ARCH}.AppImage"
copy_one cli "$CLI" "rpi-imager-cli-${IMG_ARCH}.AppImage"

echo "sync-appimages: ready in $DEST"
