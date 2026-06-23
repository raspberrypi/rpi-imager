#!/bin/sh
# Unified Debian release workflow for rpi-imager.
#
# Usage:
#   debian/release.sh status
#   debian/release.sh source [git-ref]
#   debian/release.sh appimages <arch> [--use-cache]
#   debian/release.sh arch <arch> [--use-cache]
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
  QT_CACHE=$QT_CACHE
  QT_VERSION=$QT_VERSION
  BUILDER=$BUILDER
  APPIMAGE_BUILD=$APPIMAGE_BUILD
  QT_BUILD=$QT_BUILD

Commands:
  status                 show version, artifacts, AppImages, builder
  source [git-ref]       build quilt source package (default: HEAD)
  appimages <arch>       build AppImages and sync to cache
                         --use-cache to skip build and sync staged files only
  binary <arch>          build binary packages (sbuild or local)
  arch <arch> [--use-cache]  appimages + binary
  repo                   source, then arch for each arch in RELEASE_ARCHES

AppImage build (APPIMAGE_BUILD=$APPIMAGE_BUILD):
  always     rebuild before every sync (default)
  cached     sync only; use with pre-staged AppImages

Cross-arch AppImages (foreign host): schroot/qemu when a chroot exists,
then APPIMAGE_REMOTE_<arch> as fallback:
  APPIMAGE_REMOTE_arm64=user@pi5:/home/tdewey/rpi-imager

Examples:
  cp debian/release.conf.example debian/release.conf
  debian/release.sh status
  debian/release.sh arch arm64
  debian/release.sh appimages amd64 --use-cache
  RELEASE_ARCHES="arm64 amd64" debian/release.sh repo
EOF
}

artifact_ok() {
	appimage_cache_ok "$1"
}

should_build_appimages() {
	_explicit=${1:-}

	case "$_explicit" in
		--use-cache) return 1 ;;
	esac

	case "$APPIMAGE_BUILD" in
		cached|use-cache|never) return 1 ;;
	esac
	return 0
}

cmd_status() {
	_builder=$(choose_builder "$HOST_ARCH")
	echo "version:       $VERSION (upstream $UPSTREAM)"
	echo "host arch:     $HOST_ARCH"
	echo "tree:          $TOP"
	echo "output dir:    $OUTPUT_DIR"
	echo "appimage root: $APPIMAGE_ROOT"
	echo "qt cache:      $QT_CACHE (Qt $QT_VERSION, QT_BUILD=$QT_BUILD)"
	echo "builder:       $BUILDER (next build for $HOST_ARCH: $_builder)"
	echo "appimage build: $APPIMAGE_BUILD"
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
		dir="$APPIMAGE_ROOT/$arch"
		if appimage_cache_ok "$arch"; then
			echo "  ok  $arch ($dir)"
		elif [ -d "$dir" ]; then
			echo "  ??  $arch (incomplete, $dir)"
		else
			echo "  --  $arch"
		fi
		if appimage_remote_host "$arch" 2>/dev/null; then
			echo "       remote: $APPIMAGE_REMOTE_HOST ($APPIMAGE_REMOTE_DIR)"
		fi
	done
	echo
	echo "Qt cache (desktop + cli, version $QT_VERSION):"
	for arch in arm64 amd64 armhf; do
		if qt_cache_ok "$arch"; then
			echo "  ok  $arch ($QT_CACHE/$arch/$QT_VERSION)"
		elif [ -d "$QT_CACHE/$arch" ]; then
			echo "  ??  $arch (incomplete, $QT_CACHE/$arch)"
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
	shift
	explicit=${1:-}

	if should_build_appimages "$explicit"; then
		"$TOP/debian/build-appimages.sh" "$arch"
	fi

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
