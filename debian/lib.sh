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

CHROOT_DIST=${CHROOT_DIST:-trixie}
# Chroot name suffix (dir: <dist>-<arch>-<suffix>, e.g. trixie-arm64-rpi-imager).
CHROOT_SUFFIX=${CHROOT_SUFFIX:-rpi-imager}
DEBIAN_MIRROR=${DEBIAN_MIRROR:-http://deb.debian.org/debian}
RASPBIAN_MIRROR=${RASPBIAN_MIRROR:-http://raspbian.raspberrypi.com/raspbian}
RPI_MIRROR=${RPI_MIRROR:-http://archive.raspberrypi.com/debian}
CHROOT_ARCHES=${CHROOT_ARCHES:-arm64 amd64 armhf}
# auto/local: host arch builds locally, foreign arches in an mmdebstrap chroot; chroot: force chroot
BUILDER=${BUILDER:-auto}
# auto: create missing chroots via mmdebstrap (rootless, default); 0: require manual setup
CHROOT_AUTO_CREATE=${CHROOT_AUTO_CREATE:-auto}
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
	printf '%s-%s-%s\n' "$CHROOT_DIST" "$1" "$CHROOT_SUFFIX"
}

# Put the native host arch first so AppImage/Qt builds can run on the host.
release_arch_order() {
	_native=$HOST_ARCH
	_out=""

	for _arch in $RELEASE_ARCHES; do
		if [ "$_arch" = "$_native" ]; then
			_out="$_arch"
			break
		fi
	done

	for _arch in $RELEASE_ARCHES; do
		if [ "$_arch" = "$_native" ]; then
			continue
		fi
		_out="$_out $_arch"
	done

	# shellcheck disable=SC2086
	set -- $_out
	printf '%s\n' "$@"
}

# Foreign arches need a rootless mmdebstrap chroot or APPIMAGE_REMOTE_<arch>.
# CHROOT_AUTO_CREATE=auto (default) creates missing chroots when possible.
# Returns a space-separated list of foreign RELEASE_ARCHES missing chroot/remote.
missing_release_chroots() {
	_missing=""
	for _arch in $RELEASE_ARCHES; do
		if [ "$_arch" = "$HOST_ARCH" ]; then
			continue
		fi
		if have_chroot "$_arch"; then
			continue
		fi
		if appimage_remote_host "$_arch" 2>/dev/null; then
			continue
		fi
		_missing="$_missing $_arch"
	done
	printf '%s' "$_missing"
}

# Host arch builds locally; foreign arches build in their rootless mmdebstrap chroot.
choose_builder() {
	_arch=$1
	case "$BUILDER" in
		chroot) printf '%s\n' chroot ;;
		local|auto)
			if [ "$_arch" = "$HOST_ARCH" ]; then
				printf '%s\n' local
			else
				printf '%s\n' chroot
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

system_qt6_qmake() {
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

# Minimum Qt required by src/CMakeLists.txt (find_package(Qt6 6.9 ...)).
QT_MIN_VERSION=${QT_MIN_VERSION:-6.9}

# True when version $1 (e.g. 6.8.2) is >= QT_MIN_VERSION (major.minor compare).
qt_version_ge_min() {
	_v=$1
	[ -n "$_v" ] || return 1
	_vmaj=${_v%%.*}
	_vrest=${_v#*.}
	_vmin=${_vrest%%.*}
	_minmaj=${QT_MIN_VERSION%%.*}
	_minrest=${QT_MIN_VERSION#*.}
	_minmin=${_minrest%%.*}
	case "$_vmaj$_vmin$_minmaj$_minmin" in
		*[!0-9]*) return 1 ;;
	esac
	[ "$_vmaj" -gt "$_minmaj" ] && return 0
	[ "$_vmaj" -eq "$_minmaj" ] && [ "$_vmin" -ge "$_minmin" ] && return 0
	return 1
}

system_qt6_desktop_ok() {
	_qmake=$(system_qt6_qmake) || return 1
	_ver=$("$_qmake" -query QT_VERSION 2>/dev/null) || return 1
	qt_version_ge_min "$_ver"
}

# Vendored desktop Qt and/or system Qt6 (qmake6 from apt).
qt_desktop_ready() {
	_arch=$1
	qt_desktop_ok "$_arch" || system_qt6_desktop_ok
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

qt_cache_ok() {
	_arch=$1
	qt_cli_ok "$_arch" || return 1
	if qt_desktop_build_required "$_arch"; then
		qt_desktop_ok "$_arch"
	else
		qt_desktop_ready "$_arch"
	fi
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

# Verify an AppImage's embedded runtime ELF matches the target image arch.
# appimagetool can silently embed the wrong (host) runtime on cross-packs, so
# we inspect the executable header with file(1). Returns 0 if unverifiable
# (file missing) to avoid false negatives.
appimage_elf_arch_ok() {
	_file=$1
	_img_arch=$(normalize_image_arch "$2")
	[ -f "$_file" ] || return 1
	command -v file >/dev/null 2>&1 || return 0
	_desc=$(file -Lb "$_file" 2>/dev/null) || return 1
	case "$_img_arch" in
		x86_64)  case "$_desc" in *x86-64*) return 0 ;; esac ;;
		aarch64) case "$_desc" in *"ARM aarch64"*) return 0 ;; esac ;;
		armhf)   case "$_desc" in *"ELF 32-bit"*ARM*) return 0 ;; esac ;;
	esac
	return 1
}

# True when both staged AppImages exist and their runtime ELF matches the arch.
appimage_cache_arch_ok() {
	_arch=$1
	_img_arch=$(deb_to_image_arch "$_arch")
	_dir="$APPIMAGE_ROOT/$_arch"
	appimage_elf_arch_ok "$_dir/rpi-imager-${_img_arch}.AppImage" "$_img_arch" || return 1
	appimage_elf_arch_ok "$_dir/rpi-imager-cli-${_img_arch}.AppImage" "$_img_arch" || return 1
	return 0
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

# Parallelism for cmake --build (including FetchContent sub-builds during configure).
cmake_build_jobs() {
	if [ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]; then
		printf '%s\n' "$CMAKE_BUILD_PARALLEL_LEVEL"
		return 0
	fi
	nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4
}

export_cmake_parallel() {
	export CMAKE_BUILD_PARALLEL_LEVEL="$(cmake_build_jobs)"
}

debian_build_deps_installed() {
	if [ -f "${TOP:-}/debian/control" ]; then
		(cd "$TOP" && dpkg-checkbuilddeps >/dev/null 2>&1)
		return $?
	fi
	dpkg-query -W -f='${Status}' debhelper 2>/dev/null \
		| grep -qE '^(install ok installed|install ok unpacked|install ok half-configured)$' && \
		dpkg-query -W -f='${Status}' dh-exec 2>/dev/null \
		| grep -qE '^(install ok installed|install ok unpacked|install ok half-configured)$' && \
		command -v fakeroot >/dev/null 2>&1
}

# debhelper/dh-exec for dpkg-buildpackage (.deb builds, not AppImage/Qt).
ensure_debian_build_deps() {
	if debian_build_deps_installed; then
		return 0
	fi
	echo "ensure-debian-build-deps: installing packaging tools..."
	if [ -n "${RPI_IMAGER_CHROOT:-}" ]; then
		# shellcheck disable=SC2046
		sh "$TOP/debian/chroot-apt-install.sh" $(tr '\n' ' ' <"$TOP/debian/debian-build-packages")
	elif [ "$(id -u)" -eq 0 ]; then
		apt-get update
		# shellcheck disable=SC2046
		apt-get install -y $(tr '\n' ' ' <"$TOP/debian/debian-build-packages")
	elif command -v sudo >/dev/null 2>&1; then
		sudo apt-get update
		# shellcheck disable=SC2046
		sudo apt-get install -y $(tr '\n' ' ' <"$TOP/debian/debian-build-packages")
	else
		echo "ensure-debian-build-deps: root or sudo required" >&2
		return 1
	fi
	if ! debian_build_deps_installed; then
		echo "ensure-debian-build-deps: build dependencies still unsatisfied:" >&2
		if [ -f "$TOP/debian/control" ]; then
			(cd "$TOP" && dpkg-checkbuilddeps) 2>&1 | head -5 >&2 || true
		fi
		return 1
	fi
}

# AppImage type2 runtime for a target image arch (x86_64|aarch64|armhf).
# appimagetool embeds a runtime as the AppImage's ELF header. The ARCH env var
# alone is unreliable for cross-arch packing (it falls back to the tool's own
# x86_64 runtime), so we pass an explicit --runtime-file. Cached under
# appimage-tools/ and fetched from the type2-runtime project on cache miss.
appimage_runtime_file() {
	_img_arch=$(normalize_image_arch "$1")
	_tools="${APPIMAGE_TOOLS_DIR:-$TOP/appimage-tools}"
	_dest="$_tools/runtime-$_img_arch"

	if [ -f "$_dest" ] && [ -s "$_dest" ]; then
		printf '%s\n' "$_dest"
		return 0
	fi

	mkdir -p "$_tools"
	_url="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-$_img_arch"
	if command -v curl >/dev/null 2>&1; then
		curl -fL -o "$_dest" "$_url" || { rm -f "$_dest"; return 1; }
	elif command -v wget >/dev/null 2>&1; then
		wget -O "$_dest" "$_url" || { rm -f "$_dest"; return 1; }
	else
		echo "appimage-runtime: neither curl nor wget available" >&2
		return 1
	fi

	if [ ! -s "$_dest" ]; then
		rm -f "$_dest"
		return 1
	fi
	chmod +x "$_dest" 2>/dev/null || true
	printf '%s\n' "$_dest"
}

# Host/tool arch for linuxdeploy and appimagetool (AppImage arch of the builder machine).
# Pass APPIMAGE_TOOL_ARCH as a Debian arch (amd64) or image arch (x86_64) when the
# target AppImage arch differs, e.g. cross-builds via chroot on an amd64 host.
appimage_resolve_tool_arch() {
	if [ -n "${APPIMAGE_TOOL_ARCH:-}" ]; then
		normalize_image_arch "$(deb_to_image_arch "$APPIMAGE_TOOL_ARCH")"
	else
		normalize_image_arch "$(uname -m)"
	fi
}

# appimagetool requires a .desktop file and matching icon at the AppDir root.
# linuxdeploy creates these; cross-pack (build in chroot, pack on host) must add them.
prepare_appdir_for_appimagetool() {
	_appdir=$1
	_desktop_id=$2

	_desktop_src="$_appdir/usr/share/applications/${_desktop_id}.desktop"
	if [ ! -f "$_desktop_src" ]; then
		echo "prepare_appdir_for_appimagetool: missing $_desktop_src" >&2
		return 1
	fi

	_desktop_root="$_appdir/${_desktop_id}.desktop"
	if [ ! -e "$_desktop_root" ]; then
		ln -sf "usr/share/applications/${_desktop_id}.desktop" "$_desktop_root"
	fi

	_icon=$(grep '^Icon=' "$_desktop_src" | head -1 | cut -d= -f2-)
	if [ -z "$_icon" ]; then
		echo "prepare_appdir_for_appimagetool: Icon= missing in $_desktop_src" >&2
		return 1
	fi

	if [ -e "$_appdir/${_icon}.png" ] || [ -e "$_appdir/${_icon}.svg" ] || [ -e "$_appdir/${_icon}.xpm" ]; then
		return 0
	fi

	for _candidate in \
		"$_appdir/usr/share/icons/hicolor/scalable/apps/${_icon}.svg" \
		"$_appdir/usr/share/icons/hicolor/256x256/apps/${_icon}.png" \
		"$_appdir/usr/share/icons/hicolor/128x128/apps/${_icon}.png" \
		"$_appdir/usr/share/icons/hicolor/48x48/apps/${_icon}.png"; do
		if [ -f "$_candidate" ]; then
			_ext=${_candidate##*.}
			ln -sf "${_candidate#$_appdir/}" "$_appdir/${_icon}.${_ext}"
			return 0
		fi
	done

	echo "prepare_appdir_for_appimagetool: icon $_icon not found under $_appdir" >&2
	return 1
}

# Download an AppImage helper tool to $1 (dest) from $2 (url) if missing.
appimage_download_tool() {
	_dest=$1
	_url=$2
	[ -f "$_dest" ] && return 0
	echo "Downloading $(basename "$_dest")..."
	curl -L -o "$_dest" "$_url" || { rm -f "$_dest"; return 1; }
	chmod +x "$_dest"
}

# Pack an AppDir into an AppImage with appimagetool, embedding the target-arch
# runtime (mandatory for cross-pack; ARCH alone falls back to the tool's own).
# Args: <appimagetool> <appdir> <output> <target-img-arch> <tool-img-arch> <desktop-id>
appimage_pack_with_tool() {
	_tool=$1
	_appdir=$2
	_out=$3
	_tarch=$4
	_toolarch=$5
	_desktop=$6

	export APPIMAGE_EXTRACT_AND_RUN=1
	prepare_appdir_for_appimagetool "$_appdir" "$_desktop" || return 1
	echo "Creating AppImage with appimagetool ($_toolarch) for target $_tarch..."
	if _runtime=$(appimage_runtime_file "$_tarch"); then
		echo "Using AppImage runtime for $_tarch: $_runtime"
		ARCH="$_tarch" "$_tool" --runtime-file "$_runtime" "$_appdir" "$_out"
	elif [ "$_tarch" = "$_toolarch" ]; then
		echo "Warning: no runtime file for $_tarch; using appimagetool default (native arch)" >&2
		ARCH="$_tarch" "$_tool" "$_appdir" "$_out"
	else
		echo "Error: could not obtain AppImage runtime for $_tarch (required for cross-pack)" >&2
		echo "  Place it at appimage-tools/runtime-$_tarch or enable network access." >&2
		return 1
	fi
}

# shellcheck disable=SC1091
. "$TOP/debian/chroot-lib.sh"
