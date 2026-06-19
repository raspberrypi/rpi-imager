#!/bin/sh
# Unified Debian release workflow for rpi-imager.
#
# Usage:
#   debian/release.sh status
#   debian/release.sh source [git-ref]
#   debian/release.sh appimages <arch> [--build]
#   debian/release.sh binary <arch>
#   debian/release.sh arch <arch> [--build]
#   debian/release.sh repo
#
# Configuration (first match wins):
#   1. Environment variables
#   2. debian/release.conf  (copy from debian/release.conf.example)
#
# Works without sbuild: BUILDER=auto falls back to local dpkg-buildpackage.
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

RELEASE_ARCHES=${RELEASE_ARCHES:-$HOST_ARCH}

usage() {
	cat <<EOF
Usage: debian/release.sh <command> [args]

Configuration file: $TOP/debian/release.conf (optional)
  OUTPUT_DIR=$OUTPUT_DIR
  APPIMAGE_ROOT=$APPIMAGE_ROOT
  BUILDER=$BUILDER

Commands:
  status                 show version, artifacts, AppImages, builder
  source [git-ref]       build quilt source package (default: HEAD)
  appimages <arch>       sync AppImages to cache
                         pass --build on native arch to create them first
  binary <arch>          build binary packages (sbuild or local)
  arch <arch> [--build]  appimages + binary
  repo                   source, then arch for each arch in RELEASE_ARCHES

Examples:
  cp debian/release.conf.example debian/release.conf
  debian/release.sh status
  debian/release.sh arch arm64 --build
  RELEASE_ARCHES="arm64 amd64" debian/release.sh repo
EOF
}

artifact_ok() {
	_dir=$1
	_img_arch=$2
	test -e "$_dir/rpi-imager-${_img_arch}.AppImage" && \
		test -e "$_dir/rpi-imager-cli-${_img_arch}.AppImage"
}

cmd_status() {
	_builder=$(choose_builder "$HOST_ARCH")
	echo "version:       $VERSION (upstream $UPSTREAM)"
	echo "host arch:     $HOST_ARCH"
	echo "tree:          $TOP"
	echo "output dir:    $OUTPUT_DIR"
	echo "appimage root: $APPIMAGE_ROOT"
	echo "builder:       $BUILDER (next build for $HOST_ARCH: $_builder)"
	if git -C "$TOP" diff --quiet && git -C "$TOP" diff --cached --quiet; then
		echo "git:           clean"
	else
		echo "git:           dirty (commit before tagging a release)"
	fi
	if [ -f "$TOP/debian/release.conf" ]; then
		echo "config:        debian/release.conf"
	else
		echo "config:        defaults (copy debian/release.conf.example to customise)"
	fi
	echo
	echo "source artifacts:"
	for f in \
		"${PACKAGE}_${UPSTREAM}.orig.tar.xz" \
		"${PACKAGE}_${VERSION}.debian.tar.xz" \
		"${PACKAGE}_${VERSION}.dsc" \
		"${PACKAGE}_${VERSION}_source.changes"
	do
		if [ -f "$OUTPUT_DIR/$f" ]; then
			echo "  ok  $OUTPUT_DIR/$f"
		else
			echo "  --  $OUTPUT_DIR/$f"
		fi
	done
	echo
	echo "AppImage cache:"
	for arch in arm64 amd64 armhf; do
		img=$(deb_to_image_arch "$arch")
		dir="$APPIMAGE_ROOT/$arch"
		if artifact_ok "$dir" "$img"; then
			echo "  ok  $arch ($dir)"
		elif [ -d "$dir" ]; then
			echo "  ??  $arch (incomplete, $dir)"
		else
			echo "  --  $arch"
		fi
	done
	echo
	echo "sbuild chroots (optional):"
	if have_sbuild; then
		for arch in arm64 amd64; do
			if have_chroot "$arch"; then
				echo "  ok  $(chroot_name "$arch")"
			else
				echo "  --  $(chroot_name "$arch")"
			fi
		done
	else
		echo "  --  sbuild not installed (local builds only)"
	fi
}

cmd_source() {
	ref="${1:-HEAD}"
	"$TOP/debian/build-source.sh" "$ref"
}

cmd_appimages() {
	arch=$1
	build=${2:-}
	img_arch=$(deb_to_image_arch "$arch")

	case "$build" in
		--build)
			if [ "$arch" != "$HOST_ARCH" ]; then
				echo "release: --build only supported for native arch ($HOST_ARCH)" >&2
				exit 1
			fi
			"$TOP/create-appimage.sh" --arch="$img_arch"
			"$TOP/create-appimage-cli.sh" --arch="$img_arch"
			;;
		"") ;;
		*)
			echo "release: unknown option: $build" >&2
			exit 1
			;;
	esac

	"$TOP/debian/sync-appimages.sh" "$arch"
}

cmd_binary() {
	arch=$1
	_builder=$(choose_builder "$arch")
	echo "release: using $_builder builder for $arch"
	case "$_builder" in
		sbuild) "$TOP/debian/build-sbuild.sh" binary "$arch" ;;
		local) "$TOP/debian/build-binary-local.sh" "$arch" ;;
		*)
			echo "release: unknown builder: $_builder" >&2
			exit 1
			;;
	esac
}

cmd_arch() {
	arch=$1
	shift
	cmd_appimages "$arch" "$@"
	cmd_binary "$arch"
}

cmd_repo() {
	cmd_source
	for arch in $RELEASE_ARCHES; do
		cmd_arch "$arch"
	done
	if [ -n "$DPUT_HOST" ]; then
		for changes in "$OUTPUT_DIR/${PACKAGE}_${VERSION}"*.changes; do
			[ -f "$changes" ] || continue
			echo "release: dput $DPUT_HOST $changes"
			dput "$DPUT_HOST" "$changes"
		done
	fi
}

command=${1:-}
shift || true

case "$command" in
	status) cmd_status ;;
	source) cmd_source "$@" ;;
	appimages) cmd_appimages "${1:?arch required}" "${2:-}" ;;
	binary) cmd_binary "${1:?arch required}" ;;
	arch) cmd_arch "${1:?arch required}" "${2:-}" ;;
	repo) cmd_repo ;;
	-h|--help|help|"") usage ;;
	*)
		echo "release: unknown command: $command" >&2
		usage >&2
		exit 1
		;;
esac
