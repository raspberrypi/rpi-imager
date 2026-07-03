#!/bin/sh
# Build binary .debs inside an mmdebstrap rootfs (rootless cross-build).
#
# Usage:
#   debian/build-binary-chroot.sh <arm64|amd64|armhf>
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: build-binary-chroot.sh <arch>}"

if [ "$(chroot_backend_for "$ARCH")" != mmdebstrap ]; then
	echo "build-binary-chroot: mmdebstrap chroot $(chroot_name "$ARCH") not found" >&2
	echo "build-binary-chroot: run: debian/mmdebstrap-ensure-chroot.sh $ARCH" >&2
	exit 1
fi

ensure_dirs
sh "$TOP/debian/fetch-vendor-deps.sh"
APPIMAGE_DIR="$APPIMAGE_ROOT/$ARCH" DEB_BUILD_ARCH="$ARCH" "$TOP/debian/stage-appimages.sh" all

_profiles=$(printf '%s' "$DEB_BUILD_PROFILES" | tr ' ' ',')
_parent=$(dirname "$TOP")

chroot_run "$ARCH" bash -lc \
	"cd '$TOP' && . debian/lib.sh && ensure_debian_build_deps && dpkg-buildpackage -b -uc -us -a'$ARCH' -P'$_profiles'"

# dpkg-buildpackage writes artifacts to $TOP/.. — but only $TOP (not its parent)
# is bind-mounted, so they land inside the chroot rootfs, not on the host parent.
_chroot_root=$(chroot_mmdebstrap_root "$ARCH")
_deb_src="${_chroot_root}${_parent}"

for _deb in "$_deb_src"/*_"$ARCH".deb; do
	[ -f "$_deb" ] || continue
	mv "$_deb" "$OUTPUT_DIR/"
done
for _meta in "$_deb_src"/*_"$ARCH".buildinfo "$_deb_src"/*_"$ARCH".changes; do
	[ -f "$_meta" ] || continue
	mv "$_meta" "$OUTPUT_DIR/" 2>/dev/null || true
done

if ! ls "$OUTPUT_DIR"/*_"$ARCH".deb >/dev/null 2>&1; then
	echo "build-binary-chroot: no $ARCH .debs collected from $_deb_src" >&2
	exit 1
fi

echo "build-binary-chroot: wrote $ARCH packages to $OUTPUT_DIR"
