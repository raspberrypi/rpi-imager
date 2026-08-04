#!/bin/sh
# Build the embedded (linuxfb) rpi-imager .deb for one architecture.
#
# The embedded package vendors Qt + dependencies under /opt and renders with
# linuxfb (no desktop environment). It links the target's system libraries, so
# a foreign arch must be built inside that arch's chroot; the host arch builds
# locally. The vendored release Qt (QT_CACHE) is used via --qt-root.
#
# Usage:
#   debian/build-embedded.sh <amd64|arm64|armhf>
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: build-embedded.sh <arch>}"

IMG_ARCH=$(normalize_image_arch "$ARCH")
QT_DIR=$(qt_desktop_path "$ARCH") || {
	echo "build-embedded: unknown arch: $ARCH" >&2
	exit 1
}

if [ ! -x "$QT_DIR/bin/qmake" ] && [ ! -d "$QT_DIR/plugins/platforms" ]; then
	echo "build-embedded: vendored Qt not found at $QT_DIR" >&2
	echo "build-embedded: build it first (e.g. debian/release.sh appimages $ARCH)" >&2
	exit 1
fi

if [ ! -f "$QT_DIR/plugins/platforms/libqlinuxfb.so" ]; then
	echo "build-embedded: $QT_DIR has no linuxfb platform plugin" >&2
	exit 1
fi

ensure_dirs
sh "$TOP/debian/fetch-vendor-deps.sh"

# The host arch builds in its chroot too, so the vendored tree links against
# bookworm's libraries rather than whatever the builder happens to run.
_backend=$(chroot_backend_for "$ARCH")
if [ "$_backend" = none ]; then
	echo "build-embedded: no chroot for $ARCH (need $(chroot_name "$ARCH"))" >&2
	echo "build-embedded: run: debian/mmdebstrap-ensure-chroot.sh $ARCH" >&2
	exit 1
fi
echo "build-embedded: building $ARCH inside $_backend chroot"
chroot_run "$ARCH" bash -lc \
	"cd '$TOP' && sh '$TOP/create-embedded.sh' --arch='$IMG_ARCH' --qt-root='$QT_DIR'"

# create-embedded.sh writes the .deb into $TOP (bind-mounted in the chroot, so
# it is visible on the host tree either way). Collect it into OUTPUT_DIR.
_found=0
for _deb in "$TOP"/rpi-imager-embedded_*_"$ARCH".deb; do
	[ -f "$_deb" ] || continue
	mv "$_deb" "$OUTPUT_DIR/"
	_found=1
done
# Drop the convenience symlink create-embedded.sh leaves in the tree.
rm -f "$TOP/rpi-imager-embedded.deb"

if [ "$_found" -ne 1 ]; then
	echo "build-embedded: no $ARCH embedded .deb produced" >&2
	exit 1
fi

echo "build-embedded: wrote $ARCH embedded package to $OUTPUT_DIR"
