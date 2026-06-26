#!/bin/sh
# Ensure desktop + CLI Qt builds exist in the per-arch cache (QT_CACHE).
#
# Builds on cache miss (QT_BUILD=auto, default). Installs build dependencies
# when needed. Safe to call from native host or inside schroot.
#
# armhf desktop Qt is attempted by default (QT_DESKTOP_BUILD=try). If the
# vendored build fails, system Qt6 from apt is accepted when available.
#
# Usage:
#   debian/ensure-qt.sh <arm64|amd64|armhf>
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"
. "$TOP/debian/qt-resolve.sh"

ARCH="${1:?usage: ensure-qt.sh <arm64|amd64|armhf>}"
ensure_dirs

qt_prefix() {
	printf '%s\n' "$(qt_version_tree "$ARCH")"
}

import_qt_from_opt() {
	[ "$ARCH" = "$HOST_ARCH" ] || return 1
	[ -d /opt/Qt ] || return 1
	_opt_tree="/opt/Qt/$QT_VERSION"
	[ -d "$_opt_tree" ] || return 1

	_dest=$(qt_version_tree "$ARCH")
	_desk=$(qt_desktop_gcc_dir "$ARCH")
	_cli=$(qt_cli_gcc_dir "$ARCH")
	_imported=0

	mkdir -p "$_dest"

	if [ -d "$_opt_tree/$_desk" ] && [ ! -e "$_dest/$_desk" ]; then
		ln -sfn "$_opt_tree/$_desk" "$_dest/$_desk"
		_imported=1
		echo "ensure-qt: linked desktop Qt from /opt/Qt into $_dest/$_desk"
	fi
	if [ -d "$_opt_tree/$_cli" ] && [ ! -e "$_dest/$_cli" ]; then
		ln -sfn "$_opt_tree/$_cli" "$_dest/$_cli"
		_imported=1
		echo "ensure-qt: linked CLI Qt from /opt/Qt into $_dest/$_cli"
	fi

	[ "$_imported" -eq 1 ]
}

in_schroot() {
	[ -n "${SCHROOT_CHROOT_NAME:-}" ] || [ -n "${RPI_IMAGER_CHROOT:-}" ]
}

run_apt() {
	if in_schroot || [ "$(id -u)" -eq 0 ]; then
		apt-get "$@"
	else
		sudo apt-get "$@"
	fi
}

deps_installed() {
	command -v cmake >/dev/null 2>&1 && \
		command -v ninja >/dev/null 2>&1 && \
		dpkg -s libgnutls28-dev >/dev/null 2>&1
}

install_build_deps() {
	if deps_installed; then
		return 0
	fi
	echo "ensure-qt: installing build dependencies..."
	run_apt update
	# shellcheck disable=SC2046
	run_apt install -y $(tr '\n' ' ' <"$TOP/debian/sbuild-chroot-packages")
	# Optional system Qt6 for armhf desktop fallback (best-effort).
	if [ "$ARCH" = armhf ] && ! qt_desktop_build_required "$ARCH"; then
		run_apt install -y qt6-base-dev qt6-declarative-dev 2>/dev/null || \
			echo "ensure-qt: system Qt6 dev packages not available (optional)" >&2
	fi
}

need_desktop=1
need_cli=1

import_qt_from_opt || true

if qt_desktop_ok "$ARCH"; then
	need_desktop=0
elif ! qt_desktop_build_required "$ARCH" && system_qt6_desktop_ok; then
	echo "ensure-qt: using system Qt6 for desktop ($ARCH)"
	need_desktop=0
fi
if qt_cli_ok "$ARCH"; then
	need_cli=0
fi

if [ "$need_desktop" -eq 0 ] && [ "$need_cli" -eq 0 ]; then
	case "$QT_BUILD" in
		always|force)
			need_desktop=1
			need_cli=1
			;;
		*)
			echo "ensure-qt: cache hit for $ARCH (Qt $QT_VERSION)"
			exit 0
			;;
	esac
fi

case "$QT_BUILD" in
	cached|use-cache|never)
		echo "ensure-qt: Qt cache incomplete for $ARCH (QT_BUILD=$QT_BUILD)" >&2
		echo "ensure-qt: expected desktop: $(qt_desktop_path "$ARCH") (or system qmake6)" >&2
		echo "ensure-qt: expected cli:     $(qt_cli_path "$ARCH")" >&2
		exit 1
		;;
esac

if [ "$ARCH" != "$HOST_ARCH" ] && ! in_schroot; then
	echo "ensure-qt: $ARCH Qt must be built inside the $ARCH schroot" >&2
	echo "ensure-qt: run via: debian/build-appimages.sh $ARCH" >&2
	exit 1
fi

install_build_deps
_prefix=$(qt_prefix)

if [ "$need_desktop" -eq 1 ]; then
	echo "ensure-qt: building desktop Qt $QT_VERSION for $ARCH..."
	echo "ensure-qt: install prefix $_prefix"
	if ! "$TOP/qt/build-qt.sh" \
		--version="$QT_VERSION" \
		--prefix="$_prefix" \
		--skip-dependencies; then
		if qt_desktop_build_required "$ARCH"; then
			echo "ensure-qt: desktop Qt build failed (required for $ARCH)" >&2
			exit 1
		fi
		echo "ensure-qt: desktop Qt build failed; will use system Qt6 if available" >&2
		if ! system_qt6_desktop_ok; then
			echo "ensure-qt: warning: no vendored or system desktop Qt6 for $ARCH" >&2
			echo "ensure-qt: desktop AppImage may fail; CLI builds can still proceed" >&2
		fi
	fi
fi

if [ "$need_cli" -eq 1 ]; then
	echo "ensure-qt: building CLI Qt $QT_VERSION for $ARCH..."
	"$TOP/qt/build-qt-cli.sh" \
		--version="$QT_VERSION" \
		--prefix="$_prefix" \
		--skip-dependencies
fi

if ! qt_cli_ok "$ARCH"; then
	echo "ensure-qt: CLI Qt build finished but cache check failed for $ARCH" >&2
	exit 1
fi

if qt_desktop_build_required "$ARCH" && ! qt_desktop_ok "$ARCH"; then
	echo "ensure-qt: desktop Qt cache check failed for $ARCH" >&2
	exit 1
fi

if ! qt_desktop_build_required "$ARCH" && ! qt_desktop_ready "$ARCH"; then
	echo "ensure-qt: ready for CLI on $ARCH; desktop Qt not available (optional)" >&2
	exit 0
fi

echo "ensure-qt: ready for $ARCH at $QT_CACHE/$ARCH/$QT_VERSION"
