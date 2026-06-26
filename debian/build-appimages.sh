#!/bin/sh
# Build desktop + CLI AppImages for one Debian architecture.
#
# Usage:
#   debian/build-appimages.sh <arm64|amd64|armhf>
#
# Cross-arch builds run inside the matching sbuild schroot when available.
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: build-appimages.sh <arm64|amd64|armhf>}"
IMG_ARCH=$(normalize_image_arch "$(deb_to_image_arch "$ARCH")")
CHROOT=$(chroot_name "$ARCH")

run_build_env() {
	# shellcheck disable=SC2086
	env QT_CACHE="$QT_CACHE" QT_VERSION="$QT_VERSION" APPIMAGE_ROOT="$APPIMAGE_ROOT" $*
}

run_in_build_context() {
	if [ "$ARCH" = "$HOST_ARCH" ]; then
		run_build_env "$@"
		return $?
	fi

	if have_chroot "$ARCH"; then
		if ! schroot -c "$CHROOT" -- test -d "$TOP"; then
			echo "build-appimages: $TOP not visible inside $CHROOT" >&2
			echo "build-appimages: re-run: sudo debian/sbuild-setup.sh (bind-mounts source tree)" >&2
			exit 1
		fi
		schroot -c "$CHROOT" -- bash -lc \
			"cd '$TOP' && QT_CACHE='$QT_CACHE' QT_VERSION='$QT_VERSION' APPIMAGE_ROOT='$APPIMAGE_ROOT' $(printf '%q ' "$@")"
		return $?
	fi

	if appimage_remote_host "$ARCH"; then
		echo "build-appimages: using remote builder $APPIMAGE_REMOTE_HOST for $ARCH"
		# shellcheck disable=SC2086
		ssh "$APPIMAGE_REMOTE_HOST" "cd '$APPIMAGE_REMOTE_DIR' && QT_CACHE='$QT_CACHE' QT_VERSION='$QT_VERSION' $(printf '%q ' "$@")"
		return $?
	fi

	echo "build-appimages: no schroot or remote builder for $ARCH" >&2
	echo "build-appimages: run: sudo debian/sbuild-setup.sh" >&2
	exit 1
}

ensure_dirs
echo "build-appimages: ensuring Qt for $ARCH..."
run_in_build_context "$TOP/debian/ensure-qt.sh" "$ARCH"

echo "build-appimages: desktop AppImage ($IMG_ARCH)..."
if run_in_build_context "$TOP/create-appimage.sh" "--arch=$IMG_ARCH" "--try-build-qt"; then
	:
elif [ "$ARCH" = armhf ]; then
	echo "build-appimages: desktop AppImage failed for armhf (CLI may still succeed)" >&2
else
	exit 1
fi

echo "build-appimages: CLI AppImage ($IMG_ARCH)..."
run_in_build_context "$TOP/create-appimage-cli.sh" "--arch=$IMG_ARCH"

echo "build-appimages: done for $ARCH"
