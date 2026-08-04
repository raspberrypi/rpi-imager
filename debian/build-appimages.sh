#!/bin/bash
# Build desktop + CLI AppImages for one Debian architecture.
#
# Usage:
#   debian/build-appimages.sh <arm64|amd64|armhf>
#
# Cross-arch builds run inside the matching rootless mmdebstrap chroot.
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: build-appimages.sh <arm64|amd64|armhf>}"
IMG_ARCH=$(normalize_image_arch "$(deb_to_image_arch "$ARCH")")
CHROOT=$(chroot_name "$ARCH")

# True only when this arch builds directly on the host (BUILDER=auto/local).
# With BUILDER=chroot (default) even the host arch builds in its chroot: the
# AppDir is produced inside the chroot and packed into an AppImage on the host.
_local_build() {
	[ "$(choose_builder "$ARCH")" = local ]
}

run_build_env() {
	# shellcheck disable=SC2086
	export_cmake_parallel
	env QT_CACHE="$QT_CACHE" QT_VERSION="$QT_VERSION" APPIMAGE_ROOT="$APPIMAGE_ROOT" $*
}

run_in_build_context() {
	if _local_build; then
		run_build_env "$@"
		return $?
	fi

	if ! ensure_chroot "$ARCH"; then
		exit 1
	fi

	_backend=$(chroot_backend_for "$ARCH")
	if [ "$_backend" != none ]; then
		if ! chroot_run "$ARCH" test -d "$TOP"; then
			echo "build-appimages: $TOP not visible inside $(chroot_name "$ARCH")" >&2
			echo "build-appimages: re-run: debian/mmdebstrap-ensure-chroot.sh $ARCH" >&2
			exit 1
		fi
		chroot_run "$ARCH" bash -lc \
			"cd '$TOP' && export CMAKE_BUILD_PARALLEL_LEVEL='$(cmake_build_jobs)' && QT_CACHE='$QT_CACHE' QT_VERSION='$QT_VERSION' APPIMAGE_ROOT='$APPIMAGE_ROOT' $(printf '%q ' "$@")"
		return $?
	fi

	if appimage_remote_host "$ARCH"; then
		echo "build-appimages: using remote builder $APPIMAGE_REMOTE_HOST for $ARCH"
		# shellcheck disable=SC2086
		ssh "$APPIMAGE_REMOTE_HOST" "cd '$APPIMAGE_REMOTE_DIR' && QT_CACHE='$QT_CACHE' QT_VERSION='$QT_VERSION' $(printf '%q ' "$@")"
		return $?
	fi

	echo "build-appimages: no chroot or remote builder for $ARCH (host: $HOST_ARCH)" >&2
	echo "build-appimages: expected: $(chroot_name "$ARCH") under $CHROOT_ROOT" >&2
	exit 1
}

ensure_dirs
sh "$TOP/debian/fetch-vendor-deps.sh"
echo "build-appimages: ensuring Qt for $ARCH..."
run_in_build_context "$TOP/debian/ensure-qt.sh" "$ARCH"

_pack_on_host() {
	run_build_env APPIMAGE_TOOL_ARCH="$HOST_ARCH" APPIMAGE_PACKAGING=pack \
		QT_CACHE="$QT_CACHE" QT_VERSION="$QT_VERSION" "$@"
}

_build_in_context() {
	if _local_build; then
		run_in_build_context "$@"
		return $?
	fi
	run_in_build_context env APPIMAGE_TOOL_ARCH="$HOST_ARCH" APPIMAGE_PACKAGING=build "$@"
}

echo "build-appimages: desktop AppImage ($IMG_ARCH)..."
if _local_build; then
	if run_in_build_context "$TOP/create-appimage.sh" "--arch=$IMG_ARCH" "--try-build-qt"; then
		:
	elif [ "$ARCH" = armhf ]; then
		echo "build-appimages: desktop AppImage failed for armhf (CLI may still succeed)" >&2
	else
		exit 1
	fi
else
	if _build_in_context "$TOP/create-appimage.sh" "--arch=$IMG_ARCH" "--try-build-qt"; then
		_pack_on_host "$TOP/create-appimage.sh" "--arch=$IMG_ARCH" --no-clean || exit 1
	elif [ "$ARCH" = armhf ]; then
		echo "build-appimages: desktop AppImage failed for armhf (CLI may still succeed)" >&2
	else
		exit 1
	fi
fi

echo "build-appimages: CLI AppImage ($IMG_ARCH)..."
if _local_build; then
	run_in_build_context "$TOP/create-appimage-cli.sh" "--arch=$IMG_ARCH"
else
	_build_in_context "$TOP/create-appimage-cli.sh" "--arch=$IMG_ARCH"
	_pack_on_host "$TOP/create-appimage-cli.sh" "--arch=$IMG_ARCH" --no-clean
fi

echo "build-appimages: done for $ARCH"
