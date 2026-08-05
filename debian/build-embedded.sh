#!/bin/sh
# Build the embedded (linuxfb) rpi-imager .deb for one architecture.
#
# The embedded package vendors Qt + dependencies under /opt and renders with
# linuxfb. It uses a DEDICATED Qt built with -no-opengl -qpa linuxfb (distinct
# from the desktop/cli release Qt): the target image (pi-gen-micro) carries no
# Mesa/GL or X11 -- those are far too large for a network-loaded image -- so the
# embedded Qt must not link libEGL/libGL/libX11 at all. That build is a separate
# cache variant (gcc_arm64_embedded) produced by qt/build-qt-embedded.sh.
#
# arm64 only: it is the only platform the embedded (netboot) installer targets.
#
# Usage:
#   debian/build-embedded.sh arm64
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: build-embedded.sh arm64}"

if [ "$ARCH" != arm64 ]; then
	echo "build-embedded: embedded is arm64 only (got '$ARCH')" >&2
	exit 1
fi

IMG_ARCH=$(normalize_image_arch "$ARCH")
QT_DIR=$(qt_embedded_path "$ARCH") || {
	echo "build-embedded: unknown arch: $ARCH" >&2
	exit 1
}

ensure_dirs
sh "$TOP/debian/fetch-vendor-deps.sh"

# Build inside the arch's chroot so the vendored tree links against bookworm's
# libraries rather than whatever the builder happens to run.
_backend=$(chroot_backend_for "$ARCH")
if [ "$_backend" = none ]; then
	echo "build-embedded: no chroot for $ARCH (need $(chroot_name "$ARCH"))" >&2
	echo "build-embedded: run: debian/mmdebstrap-ensure-chroot.sh $ARCH" >&2
	exit 1
fi

# Build the dedicated -no-opengl embedded Qt on cache miss. Check by file
# presence rather than qt_embedded_ok(): that runs `qmake -query`, but the
# cached qmake is the target arch (arm64) and this orchestrator runs on the
# host, so executing it would always fail and force a needless full rebuild.
if [ ! -x "$QT_DIR/bin/qmake" ] || [ ! -f "$QT_DIR/plugins/platforms/libqlinuxfb.so" ]; then
	echo "build-embedded: building -no-opengl embedded Qt $QT_VERSION for $ARCH inside $_backend chroot..."
	export_cmake_parallel
	chroot_run "$ARCH" bash -lc \
		"cd '$TOP' && export CMAKE_BUILD_PARALLEL_LEVEL='$(cmake_build_jobs)' && sh '$TOP/qt/build-qt-embedded.sh' --version='$QT_VERSION' --prefix='$(qt_version_tree "$ARCH")' --skip-dependencies --unprivileged"
fi

if [ ! -f "$QT_DIR/plugins/platforms/libqlinuxfb.so" ]; then
	echo "build-embedded: $QT_DIR has no linuxfb platform plugin after build" >&2
	exit 1
fi

echo "build-embedded: building $ARCH package inside $_backend chroot"
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
