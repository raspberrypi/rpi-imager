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

QT_CACHE=${QT_CACHE:-.debian/qt}
QT_CACHE=$(resolve_repo_path "$QT_CACHE")
QT_VERSION=${QT_VERSION:-6.11.1}
# auto: build Qt on cache miss (default); cached: require pre-built Qt; always: force rebuild
QT_BUILD=${QT_BUILD:-auto}

SBUILD_DIST=${SBUILD_DIST:-trixie}
SBUILD_CHROOT_SUFFIX=${SBUILD_CHROOT_SUFFIX:-sbuild}
SBUILD_DEBIAN_MIRROR=${SBUILD_DEBIAN_MIRROR:-http://deb.debian.org/debian}
SBUILD_RASPBIAN_MIRROR=${SBUILD_RASPBIAN_MIRROR:-http://raspbian.raspberrypi.com/raspbian}
SBUILD_RPI_MIRROR=${SBUILD_RPI_MIRROR:-http://archive.raspberrypi.com/debian}
SBUILD_CHROOT_ARCHES=${SBUILD_CHROOT_ARCHES:-arm64 amd64 armhf}
BUILDER=${BUILDER:-auto}
DEB_BUILD_PROFILES=${DEB_BUILD_PROFILES:-desktop cli}
DPUT_HOST=${DPUT_HOST:-}
# always: rebuild AppImages every time (default); cached: sync staged cache only
APPIMAGE_BUILD=${APPIMAGE_BUILD:-always}

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

# Normalise kernel uname values on 32-bit Raspberry Pi OS to Debian armhf.
normalize_image_arch() {
	case "$1" in
		amd64) printf '%s\n' x86_64 ;;
		arm64) printf '%s\n' aarch64 ;;
		armhf|armv6l|armv7l) printf '%s\n' armhf ;;
		x86_64|aarch64) printf '%s\n' "$1" ;;
		*) printf '%s\n' "$1" ;;
	esac
}

qt_desktop_gcc_dir() {
	_arch=$1
	case "$_arch" in
		amd64) printf '%s\n' gcc_64 ;;
		arm64) printf '%s\n' gcc_arm64 ;;
		armhf) printf '%s\n' gcc_arm32 ;;
		*) return 1 ;;
	esac
}

qt_cli_gcc_dir() {
	_arch=$1
	case "$_arch" in
		amd64) printf '%s\n' gcc_64_cli ;;
		arm64) printf '%s\n' gcc_arm64_cli ;;
		armhf) printf '%s\n' gcc_arm32_cli ;;
		*) return 1 ;;
	esac
}

qt_version_tree() {
	_arch=$1
	printf '%s/%s/%s\n' "$QT_CACHE" "$_arch" "$QT_VERSION"
}

qt_desktop_path() {
	_arch=$1
	_gcc=$(qt_desktop_gcc_dir "$_arch") || return 1
	printf '%s/%s\n' "$(qt_version_tree "$_arch")" "$_gcc"
}

qt_cli_path() {
	_arch=$1
	_gcc=$(qt_cli_gcc_dir "$_arch") || return 1
	printf '%s/%s\n' "$(qt_version_tree "$_arch")" "$_gcc"
}

qt_qmake_ok() {
	_dir=$1
	[ -x "$_dir/bin/qmake" ] || return 1
	_built=$("$_dir/bin/qmake" -query QT_VERSION 2>/dev/null) || return 1
	[ "$_built" = "$QT_VERSION" ]
}

qt_desktop_ok() {
	qt_qmake_ok "$(qt_desktop_path "$1")"
}

qt_cli_ok() {
	qt_qmake_ok "$(qt_cli_path "$1")"
}

qt_cache_ok() {
	qt_desktop_ok "$1" && qt_cli_ok "$1"
}

ensure_dirs() {
	mkdir -p "$OUTPUT_DIR" "$APPIMAGE_ROOT" \
		"$QT_CACHE/arm64" "$QT_CACHE/amd64" "$QT_CACHE/armhf"
}

appimage_cache_ok() {
	_arch=$1
	_img_arch=$(deb_to_image_arch "$_arch")
	_dir="$APPIMAGE_ROOT/$_arch"
	test -e "$_dir/rpi-imager-${_img_arch}.AppImage" && \
		test -e "$_dir/rpi-imager-cli-${_img_arch}.AppImage"
}

# Parse APPIMAGE_REMOTE_<arch> into APPIMAGE_REMOTE_HOST and APPIMAGE_REMOTE_DIR.
# Formats: user@host  or  user@host:/path/to/rpi-imager
# APPIMAGE_REMOTE_DIR_<arch> overrides the path when host-only form is used.
appimage_remote_host() {
	_arch=$1
	_spec_var="APPIMAGE_REMOTE_${_arch}"
	_dir_var="APPIMAGE_REMOTE_DIR_${_arch}"

	eval "_spec=\${$_spec_var:-}"
	eval "_dir_override=\${$_dir_var:-}"

	APPIMAGE_REMOTE_HOST=
	APPIMAGE_REMOTE_DIR=

	[ -n "$_spec" ] || return 1

	case "$_spec" in
		*:*)
			APPIMAGE_REMOTE_HOST=${_spec%%:*}
			APPIMAGE_REMOTE_DIR=${_spec#*:}
			;;
		*)
			APPIMAGE_REMOTE_HOST=$_spec
			APPIMAGE_REMOTE_DIR=${_dir_override:-$TOP}
			;;
	esac

	[ -n "$APPIMAGE_REMOTE_HOST" ] || return 1
	return 0
}
