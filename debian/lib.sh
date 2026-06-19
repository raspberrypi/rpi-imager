#!/bin/sh
# Shared path and tooling configuration for debian/release.sh.
#
# Source from other scripts after setting TOP:
#   TOP=$(cd "$(dirname "$0")/.." && pwd)
#   . "$TOP/debian/lib.sh"
#
# Optional overrides: debian/release.conf (copy from release.conf.example)
# Environment variables with the same names take precedence over the file.

if [ -n "${RPI_IMAGER_LIB_LOADED:-}" ]; then
	return 0 2>/dev/null || exit 0
fi
RPI_IMAGER_LIB_LOADED=1

: "${TOP:?TOP must be set before sourcing debian/lib.sh}"

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

resolve_repo_path() {
	_path=$1
	case "$_path" in
		/*) printf '%s\n' "$_path" ;;
		*) printf '%s/%s\n' "$TOP" "$_path" ;;
	esac
}

OUTPUT_DIR=${OUTPUT_DIR:-out/debian}
OUTPUT_DIR=$(resolve_repo_path "$OUTPUT_DIR")

APPIMAGE_ROOT=${APPIMAGE_ROOT:-.debian/appimages}
APPIMAGE_ROOT=$(resolve_repo_path "$APPIMAGE_ROOT")

SBUILD_DIST=${SBUILD_DIST:-trixie}
SBUILD_CHROOT_SUFFIX=${SBUILD_CHROOT_SUFFIX:-sbuild}
BUILDER=${BUILDER:-auto}
DEB_BUILD_PROFILES=${DEB_BUILD_PROFILES:-desktop cli}
DPUT_HOST=${DPUT_HOST:-}

CHANGELOG="$TOP/debian/changelog"
PACKAGE=$(dpkg-parsechangelog -l"$CHANGELOG" -SSource)
VERSION=$(dpkg-parsechangelog -l"$CHANGELOG" -SVersion)
UPSTREAM=${VERSION%%-*}
HOST_ARCH=$(dpkg --print-architecture)

chroot_name() {
	printf '%s-%s-%s\n' "$SBUILD_DIST" "$1" "$SBUILD_CHROOT_SUFFIX"
}

have_sbuild() {
	command -v sbuild >/dev/null 2>&1
}

have_chroot() {
	_arch=$1
	schroot -l 2>/dev/null | grep -q "^$(chroot_name "$_arch")\$"
}

choose_builder() {
	_arch=$1
	case "$BUILDER" in
		local) printf '%s\n' local ;;
		sbuild) printf '%s\n' sbuild ;;
		auto)
			if have_sbuild && have_chroot "$_arch"; then
				printf '%s\n' sbuild
			else
				printf '%s\n' local
			fi
			;;
		*) printf '%s\n' "$BUILDER" ;;
	esac
}

deb_to_image_arch() {
	case "$1" in
		amd64) printf '%s\n' x86_64 ;;
		arm64) printf '%s\n' aarch64 ;;
		armhf) printf '%s\n' armhf ;;
		*) printf '%s\n' "$1" ;;
	esac
}

ensure_dirs() {
	mkdir -p "$OUTPUT_DIR" "$APPIMAGE_ROOT"
}
