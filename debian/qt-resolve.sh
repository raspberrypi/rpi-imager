#!/bin/sh
# Shared Qt6 discovery for release AppImage builds.
#
# Vendored Qt in QT_CACHE is preferred; system Qt6 (qmake6) is a fallback,
# especially for armhf desktop where building Qt may be skipped or fail.

# shellcheck disable=SC2034
QT_RESOLVE_SOURCE=

qt6_qmake() {
	if [ -n "${QT_RESOLVE_QMAKE:-}" ] && [ -x "$QT_RESOLVE_QMAKE" ]; then
		printf '%s\n' "$QT_RESOLVE_QMAKE"
		return 0
	fi
	if command -v qmake6 >/dev/null 2>&1; then
		command -v qmake6
		return 0
	fi
	if command -v qmake >/dev/null 2>&1; then
		_ver=$(qmake -query QT_VERSION 2>/dev/null) || return 1
		case "$_ver" in 6.*) command -v qmake; return 0 ;; esac
	fi
	return 1
}

qt6_version_ok() {
	_ver=$1
	[ -n "$_ver" ] && [ "$_ver" = "$QT_VERSION" ]
}

qt6_query() {
	_qmake=$1
	_key=$2
	"$_qmake" -query "$_key" 2>/dev/null
}

# Resolve desktop Qt directory for an AppImage image arch (x86_64, aarch64, armhf).
# Sets QT_RESOLVE_DIR and QT_RESOLVE_SOURCE (cache|env|system).
qt_resolve_desktop_dir() {
	_image_arch=$1
	_qt_dir=""
	_source=""

	case "$_image_arch" in
		x86_64) _deb_arch=amd64; _gcc=gcc_64 ;;
		aarch64) _deb_arch=arm64; _gcc=gcc_arm64 ;;
		armhf|armv6l|armv7l) _deb_arch=armhf; _gcc=gcc_arm32 ;;
		*)
			echo "qt-resolve: unsupported image arch: $_image_arch" >&2
			return 1
			;;
	esac

	if [ -n "${QT_ROOT_ARG:-}" ]; then
		_qt_dir=$QT_ROOT_ARG
		_source=arg
	elif [ -n "${Qt6_ROOT:-}" ]; then
		_qt_dir=$Qt6_ROOT
		_source=env
	else
		if [ -n "${QT_CACHE:-}" ] && [ -d "$QT_CACHE/$_deb_arch" ]; then
			_tree=$(find -L "$QT_CACHE/$_deb_arch" -maxdepth 1 -type d -name "6.*" | sort -V | tail -n 1)
			if [ -n "$_tree" ] && [ -d "$_tree/$_gcc" ]; then
				_qt_dir="$_tree/$_gcc"
				_source=cache
			fi
		fi
		if [ -z "$_qt_dir" ] && [ -d /opt/Qt ]; then
			_tree=$(find -L /opt/Qt -maxdepth 1 -type d -name "6.*" | sort -V | tail -n 1)
			if [ -n "$_tree" ] && [ -d "$_tree/$_gcc" ]; then
				_qt_dir="$_tree/$_gcc"
				_source=opt
			fi
		fi
	fi

	if [ -z "$_qt_dir" ]; then
		_qmake=$(qt6_qmake) || return 1
		_prefix=$(qt6_query "$_qmake" QT_INSTALL_PREFIX) || return 1
		_ver=$(qt6_query "$_qmake" QT_VERSION) || return 1
		case "$_ver" in 6.*) ;; *) return 1 ;; esac
		_qt_dir=$_prefix
		_source=system
	fi

	if [ -f "$_qt_dir/bin/qmake" ]; then
		:
	elif qt6_qmake >/dev/null 2>&1; then
		:
	else
		return 1
	fi

	QT_RESOLVE_DIR=$_qt_dir
	QT_RESOLVE_SOURCE=$_source
	printf '%s\n' "$_qt_dir"
}

qt_desktop_build_required() {
	_arch=$1
	case "$_arch" in
		armhf)
			case "${QT_DESKTOP_BUILD:-try}" in
				try|optional|best-effort) return 1 ;;
				*) return 0 ;;
			esac
			;;
		*) return 0 ;;
	esac
}
