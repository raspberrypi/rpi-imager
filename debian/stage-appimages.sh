#!/bin/sh
# Stage pre-built AppImages for desktop/cli .deb profiles.
#
# AppImages are not part of the quilt orig tarball. Before a binary build,
# place artifacts in APPIMAGE_DIR (default: package root) under the names
# expected by debian/*.install, or use the Raspberry_Pi_Imager-* naming
# from create-appimage.sh / create-appimage-cli.sh.
#
# Usage:
#   APPIMAGE_DIR=../artifacts debian/stage-appimages.sh desktop
#   APPIMAGE_DIR=../artifacts debian/stage-appimages.sh cli
#   debian/stage-appimages.sh all
set -eu

TOP="$(cd "$(dirname "$0")/.." && pwd)"
APPIMAGE_DIR="${APPIMAGE_DIR:-$TOP}"
PROFILE="${1:-all}"

deb_arch_to_image_arch() {
	case "$1" in
		amd64) echo x86_64 ;;
		arm64) echo aarch64 ;;
		armhf) echo armhf ;;
		x86_64|aarch64|armhf) echo "$1" ;;
		*)
			echo "stage-appimages: unknown Debian arch: $1" >&2
			return 1
			;;
	esac
}

usage() {
	echo "Usage: $0 [desktop|cli|all]" >&2
	exit 1
}

resolve_artifact() {
	canonical="$1"
	glob_pattern="$2"

	# Prefer explicit build outputs over pre-existing packaging symlinks.
	# shellcheck disable=SC2086
	matches=$(ls -1t $APPIMAGE_DIR/$glob_pattern 2>/dev/null || true)
	count=0
	match=""
	for candidate in $matches; do
		if [ "$count" -eq 0 ]; then
			match=$candidate
		fi
		count=$((count + 1))
	done

	if [ "$count" -ge 1 ]; then
		if [ "$count" -gt 1 ]; then
			echo "stage-appimages: multiple matches for $glob_pattern; using newest: $match" >&2
		fi
		printf '%s\n' "$match"
		return 0
	fi

	if [ -f "$APPIMAGE_DIR/$canonical" ]; then
		printf '%s\n' "$APPIMAGE_DIR/$canonical"
		return 0
	fi

	if [ -L "$APPIMAGE_DIR/$canonical" ]; then
		target=$(readlink "$APPIMAGE_DIR/$canonical")
		case "$target" in
			/*) resolved=$target ;;
			*) resolved="$APPIMAGE_DIR/$target" ;;
		esac
		if [ -f "$resolved" ]; then
			printf '%s\n' "$resolved"
			return 0
		fi
	fi

	echo "stage-appimages: missing $canonical (tried $APPIMAGE_DIR/$glob_pattern)" >&2
	return 1
}

link_artifact() {
	dest_name="$1"
	src="$2"
	dest="$TOP/$dest_name"

	if [ -e "$dest" ]; then
		current=$(readlink -f "$dest" 2>/dev/null || true)
		new=$(readlink -f "$src" 2>/dev/null || true)
		if [ -n "$current" ] && [ "$current" = "$new" ]; then
			echo "stage-appimages: $dest_name already staged"
			return 0
		fi
	fi

	rm -f "$dest"
	if [ "$(dirname "$src")" = "$TOP" ]; then
		ln -s "$(basename "$src")" "$dest"
	else
		ln -s "$src" "$dest"
	fi
	echo "stage-appimages: $dest_name -> $src"
}

stage_desktop() {
	img_arch=$(deb_arch_to_image_arch "${DEB_HOST_ARCH:-$(dpkg-architecture -qDEB_HOST_ARCH)}")
	canonical="rpi-imager-${img_arch}.AppImage"
	src=$(resolve_artifact "$canonical" "Raspberry_Pi_Imager-*-desktop-${img_arch}.AppImage")
	link_artifact "$canonical" "$src"
}

stage_cli() {
	img_arch=$(deb_arch_to_image_arch "${DEB_HOST_ARCH:-$(dpkg-architecture -qDEB_HOST_ARCH)}")
	canonical="rpi-imager-cli-${img_arch}.AppImage"
	src=$(resolve_artifact "$canonical" "Raspberry_Pi_Imager-*-cli-${img_arch}.AppImage")
	link_artifact "$canonical" "$src"
}

case "$PROFILE" in
	desktop) stage_desktop ;;
	cli) stage_cli ;;
	all)
		stage_desktop
		stage_cli
		;;
	*)
		usage
		;;
esac
