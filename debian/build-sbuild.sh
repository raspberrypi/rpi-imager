#!/bin/sh
# Build binary .debs inside sbuild.
#
# Usage:
#   debian/build-sbuild.sh binary <arm64|amd64|armhf>
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

CMD="${1:?usage: build-sbuild.sh binary <arch>}"
ARCH="${2:?usage: build-sbuild.sh binary <arch>}"

if [ "$CMD" != binary ]; then
	echo "build-sbuild: unknown command: $CMD" >&2
	exit 1
fi

if ! have_chroot "$ARCH"; then
	echo "build-sbuild: chroot $(chroot_name "$ARCH") not found" >&2
	exit 1
fi

if ! have_schroot_chroot "$ARCH"; then
	echo "build-sbuild: schroot $(chroot_name "$ARCH") required for sbuild (mmdebstrap is AppImage-only)" >&2
	echo "build-sbuild: run: sudo debian/sbuild-setup.sh $ARCH" >&2
	exit 1
fi

ensure_dirs
_dsc="$OUTPUT_DIR/${PACKAGE}_${VERSION}.dsc"
if [ ! -f "$_dsc" ]; then
	echo "build-sbuild: missing source package $_dsc (run build-source first)" >&2
	exit 1
fi

APPIMAGE_DIR="$APPIMAGE_ROOT/$ARCH" APPIMAGE_DIR="$APPIMAGE_DIR" "$TOP/debian/stage-appimages.sh" all

_profiles=$(printf '%s' "$DEB_BUILD_PROFILES" | tr ' ' ',')
_chroot=$(chroot_name "$ARCH")

sbuild \
	-A "$ARCH" \
	--host=$ARCH \
	--profiles="$_profiles" \
	--build-dir="$OUTPUT_DIR/sbuild" \
	"$_dsc"

for _deb in "$OUTPUT_DIR"/sbuild/*_"$ARCH".deb; do
	[ -f "$_deb" ] || continue
	mv "$_deb" "$OUTPUT_DIR/"
done

echo "build-sbuild: wrote $ARCH packages to $OUTPUT_DIR"
