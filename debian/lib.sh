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
# The Qt version is selected in exactly one place: QT_VERSION_DEFAULT in
# qt/qt-build-common.sh, which the qt/build-qt*.sh scripts this pipeline invokes
# also read. Extract it rather than sourcing that file — it sets ARCH, PLATFORM
# and friends, which would clobber callers of this library.
if [ -z "${QT_VERSION:-}" ]; then
	QT_VERSION=$(sed -n 's/^QT_VERSION_DEFAULT="\([^"]*\)".*/\1/p' \
		"$TOP/qt/qt-build-common.sh" 2>/dev/null | head -1)
	if [ -z "$QT_VERSION" ]; then
		echo "lib.sh: could not read QT_VERSION_DEFAULT from qt/qt-build-common.sh" >&2
		return 1 2>/dev/null || exit 1
	fi
fi
# auto: build Qt on cache miss (default); cached: require pre-built Qt; always: force rebuild
QT_BUILD=${QT_BUILD:-auto}

# bookworm (glibc 2.36) is the baseline for all arches: modern enough for Qt
# 6.11 / liburing 2.2+ yet old enough that the resulting AppImages/.debs stay
# portable to current Debian/Ubuntu/Raspberry Pi OS releases.
CHROOT_DIST=${CHROOT_DIST:-bookworm}
# Chroot name suffix (dir: <dist>-<arch>-<suffix>, e.g. bookworm-arm64-rpi-imager).
CHROOT_SUFFIX=${CHROOT_SUFFIX:-rpi-imager}
DEBIAN_MIRROR=${DEBIAN_MIRROR:-http://deb.debian.org/debian}
RASPBIAN_MIRROR=${RASPBIAN_MIRROR:-http://raspbian.raspberrypi.com/raspbian}
RPI_MIRROR=${RPI_MIRROR:-http://archive.raspberrypi.com/debian}
CHROOT_ARCHES=${CHROOT_ARCHES:-arm64 amd64 armhf}
# Every arch, including the host, builds in its bookworm mmdebstrap chroot.
# There is deliberately no host-native build path: a package built against the
# host's libraries is not the package it claims to be. Building on a newer
# glibc than bookworm yields binaries that will not start on bookworm at all,
# and building on an older one silently drops features -- liburing < 2.2 loses
# io_uring, for instance -- while the artifact still gets labelled for
# bookworm. The rootless mmdebstrap chroots need no sudo, so there is no
# environment that cannot use them.
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

# Every arch needs a rootless mmdebstrap chroot or APPIMAGE_REMOTE_<arch>,
# the host arch included. CHROOT_AUTO_CREATE=auto (default) creates missing
# chroots when possible. Returns a space-separated list of RELEASE_ARCHES
# missing a chroot/remote.
missing_release_chroots() {
	_missing=""
	for _arch in $RELEASE_ARCHES; do
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

qt_embedded_gcc_dir() {
	_arch=$1
	case "$_arch" in
		amd64) printf '%s\n' gcc_64_embedded ;;
		arm64) printf '%s\n' gcc_arm64_embedded ;;
		armhf) printf '%s\n' gcc_arm32_embedded ;;
		*) return 1 ;;
	esac
}

qt_version_tree() {
	_arch=$1
	printf '%s/%s/%s\n' "$QT_CACHE" "$_arch" "$QT_VERSION"
}

qt_embedded_path() {
	_arch=$1
	_gcc=$(qt_embedded_gcc_dir "$_arch") || return 1
	printf '%s/%s\n' "$(qt_version_tree "$_arch")" "$_gcc"
}

qt_embedded_ok() {
	qt_qmake_ok "$(qt_embedded_path "$1")"
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

# Minimum Qt accepted from a system install. Read from the find_package(Qt6 ...)
# call in src/CMakeLists.txt, which is what actually enforces it, so the two
# cannot drift.
if [ -z "${QT_MIN_VERSION:-}" ]; then
	QT_MIN_VERSION=$(sed -n \
		's/.*find_package(Qt6 \([0-9][0-9.]*\).*/\1/p' \
		"$TOP/src/CMakeLists.txt" 2>/dev/null | head -1)
	QT_MIN_VERSION=${QT_MIN_VERSION:-6.9}
fi

# True when version $1 is >= QT_MIN_VERSION (major.minor compare).
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

# Libraries that must always come from the host system and are never bundled.
# Bundling these couples the AppImage to the build host's C library, graphics
# stack or session bus; libsystemd/libdbus in particular broke lsblk and udisks
# integration (#1304, #1577). Callers must declare the corresponding Debian
# packages in debian/control instead.
appimage_lib_excluded() {
	case "$1" in
	# glibc and the dynamic loader
	ld-linux*|libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*) return 0 ;;
	libresolv.so.*|libnsl.so.*|libutil.so.*|libcrypt.so.*|libanl.so.*) return 0 ;;
	libthread_db.so.*|libnss_*|libBrokenLocale.so.*) return 0 ;;
	# compiler runtimes: must match the host libstdc++ ABI
	libstdc++.so.*|libgcc_s.so.*|libatomic.so.*) return 0 ;;
	# GPU stack: must match the host's drivers
	libGL.so.*|libGLX.so.*|libEGL.so.*|libOpenGL.so.*|libGLdispatch.so.*) return 0 ;;
	libglapi.so.*|libgbm.so.*|libdrm.so.*|libvulkan.so.*) return 0 ;;
	# X protocol bindings that stay external. libxcb.so.1 owns the connection
	# and libX11/libX11-xcb sit on top of it; the -dri2/-dri3/-present/-glx
	# bindings are driven by the host's Mesa libGL, which we also keep external
	# (above), so bundling them would risk two copies in one process. libxcb.so.1
	# and the DRI/GLX bindings are what the AppImage excludelist names; the
	# libX*.so.* glob is broader than that and predates this clause, kept as-is
	# because Xlib-level libraries have never been the ones we needed to bundle.
	libxcb.so.*|libxcb-glx.so.*|libxcb-dri2.so.*|libxcb-dri3.so.*) return 0 ;;
	libxcb-present.so.*|libX11-xcb.so.*|libX*.so.*) return 0 ;;
	# Everything else named libxcb-<extension> is a client-side binding to an X
	# protocol extension, generated from the protocol XML, or a small xcb-util
	# helper. None of them own the session: they marshal requests through
	# whatever libxcb.so.1 the host provides, carry no versioned symbols, and
	# sit far below our glibc baseline. Bundling them is what the AppImage
	# excludelist expects -- it names libxcb.so.1, libX11* and the DRI pair, and
	# pointedly not the helpers.
	#
	# Leaving them external broke the AppImage outright (#1719). Qt >= 6.5 makes
	# libxcb-cursor a hard DT_NEEDED of libQt6XcbQpa, and Qt is close to its only
	# consumer, so distros have no other reason to pull it in: libxcb-cursor0 is
	# in Ubuntu universe and absent by default on Linux Mint. The QPA plugin then
	# failed to load and the app aborted before opening a window. The other
	# helpers we need -- icccm, image, keysyms, randr, render, render-util,
	# shape, shm, sync, xfixes, xkb -- were only working by luck, because desktop
	# distros install them for other packages. This clause fixes the class rather
	# than the one instance that got reported.
	#
	# Deliberately still external: libxkbcommon* (below). Bundling it is
	# defensible on the same reasoning, but it parses keymaps for every session
	# including Wayland, so the blast radius is keyboard input rather than
	# cursors, and every real desktop already ships it.
	libxcb-*.so.*) return 1 ;;
	# display server and input: must match the running X/Wayland session
	libxkbcommon.so.*|libxkbcommon-x11.so.*|libwayland-*.so.*) return 0 ;;
	libinput.so.*|libmtdev.so.*|libevdev.so.*|libwacom.so.*) return 0 ;;
	# session and device integration (#1304, #1577)
	libsystemd.so.*|libdbus-1.so.*|libcap.so.*|libudev.so.*|libselinux.so.*) return 0 ;;
	# font stack: must match the host's fontconfig cache and configuration
	libfontconfig.so.*|libfreetype.so.*|libexpat.so.*) return 0 ;;
	esac
	return 1
}

# Multiarch triplet used to locate target-architecture system libraries. The
# build stage runs inside the target-arch chroot, so the local triplet is the
# one we want.
appimage_multiarch_triplet() {
	_t=""
	if command -v dpkg-architecture >/dev/null 2>&1; then
		_t=$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || true)
	fi
	if [ -z "$_t" ] && command -v gcc >/dev/null 2>&1; then
		_t=$(gcc -print-multiarch 2>/dev/null || true)
	fi
	if [ -z "$_t" ]; then
		for _d in /usr/lib/*-linux-gnu*; do
			[ -d "$_d" ] || continue
			_t=$(basename "$_d")
			break
		done
	fi
	[ -n "$_t" ] || return 1
	printf '%s\n' "$_t"
}

appimage_list_elf_objects() {
	find "$1/usr/bin" "$1/usr/lib" "$1/usr/plugins" "$1/usr/qml" \
		-type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) 2>/dev/null
}

embedded_list_elf_objects() {
	find "$1/bin" "$1/lib" "$1/plugins" "$1/qml" \
		-type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) 2>/dev/null
}

# Libraries the embedded package takes from the host. Much narrower than
# appimage_lib_excluded(): the embedded tree is deliberately self-contained
# under /opt, so only the C library, the compiler runtime, the GPU stack and
# libsystemd stay external. libsystemd in particular must come from the host to
# work with DBus (#1304).
embedded_lib_excluded() {
	case "$1" in
	ld-linux*|libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*) return 0 ;;
	libresolv.so.*|libnsl.so.*|libutil.so.*|libcrypt.so.*|libanl.so.*) return 0 ;;
	libthread_db.so.*|libnss_*|libBrokenLocale.so.*) return 0 ;;
	libstdc++.so.*|libgcc_s.so.*) return 0 ;;
	libGL.so.*|libGLX.so.*|libEGL.so.*|libOpenGL.so.*|libGLdispatch.so.*) return 0 ;;
	libglapi.so.*|libvulkan.so.*) return 0 ;;
	libX11.so.*|libX11-xcb.so.*|libxcb*.so.*|libwayland-*.so.*) return 0 ;;
	libsystemd.so.*) return 0 ;;
	esac
	return 1
}

# Shared worker for the two closure helpers. Completes the transitive
# DT_NEEDED closure of the objects listed by $2 <root> into $1, skipping
# sonames rejected by the predicate $3 and resolving against $4. Reads ELF
# headers with readelf rather than shelling out to ldd, so it works unchanged
# on a foreign-architecture tree. Call after pruning unwanted plugins, so
# their dependencies are not dragged in.
deploy_lib_closure_core() {
	_libdir=$1
	_lister=$2
	_root=$3
	_excl=$4
	_searchdirs=$5
	mkdir -p "$_libdir"

	_round=0
	_missing=""
	while [ "$_round" -lt 16 ]; do
		_round=$((_round + 1))
		_added=0
		# shellcheck disable=SC2046
		for _obj in $("$_lister" "$_root"); do
			# shellcheck disable=SC2046
			for _need in $(readelf -d "$_obj" 2>/dev/null | \
				sed -n 's/.*(NEEDED)[^[]*\[\([^]]*\)\].*/\1/p'); do
				[ -n "$_need" ] || continue
				[ -e "$_libdir/$_need" ] && continue
				"$_excl" "$_need" && continue
				_found=""
				for _dir in $_searchdirs; do
					[ -f "$_dir/$_need" ] || continue
					cp -L "$_dir/$_need" "$_libdir/$_need"
					echo "  bundled $_need (from $_dir)"
					_found=1
					_added=1
					break
				done
				if [ -z "$_found" ]; then
					case " $_missing " in
					*" $_need "*) ;;
					*) _missing="$_missing $_need" ;;
					esac
				fi
			done
		done
		[ "$_added" -eq 0 ] && break
	done

	if [ "$_round" -ge 16 ]; then
		echo "Warning: library closure did not converge after 16 rounds" >&2
	fi
	if [ -n "$_missing" ]; then
		echo "Error: unresolved libraries, not excluded and not found on the build system:" >&2
		for _m in $_missing; do
			echo "  $_m" >&2
		done
		echo "  Add the providing package to debian/chroot-packages, or add the" >&2
		echo "  soname to the exclusion predicate and declare it in debian/control." >&2
		return 1
	fi
}

# Search path for resolving DT_NEEDED against the target's system libraries.
deploy_lib_search_dirs() {
	_qtlib=${1:-}
	_triplet=$(appimage_multiarch_triplet || true)
	_dirs=""
	[ -n "$_qtlib" ] && _dirs="$_qtlib"
	if [ -n "$_triplet" ]; then
		_dirs="$_dirs /usr/lib/$_triplet /lib/$_triplet"
	fi
	printf '%s /usr/lib /lib\n' "$_dirs"
}

appimage_deploy_lib_closure() {
	_appdir=$1
	echo "Bundling library closure (triplet: $(appimage_multiarch_triplet || echo unknown))..."
	deploy_lib_closure_core "$_appdir/usr/lib" appimage_list_elf_objects \
		"$_appdir" appimage_lib_excluded "$(deploy_lib_search_dirs "${2:-}")"
}

# Complete the closure of the vendored /opt tree.
#
# The explicit cp -d list in create-embedded.sh is deliberately curated: this
# package is often fetched over the network, so payload size matters. This does
# not replace that curation -- it is purely additive, and only copies libraries
# something still in the tree actually names in DT_NEEDED. Because it runs
# after the pruning step, dependencies of removed plugins are not pulled back
# in, so the result stays close to the curated set: it adds the Qt libraries
# the wholesale `cp -r` of the QML tree references but the list forgot.
#
# If this ever regresses size unacceptably, the way back is to drop the
# embedded_deploy_lib_closure call from create-embedded.sh and go back to
# curating by hand -- but then prune the QML tree to match, or the plugins will
# reference libraries that are not there. Compare with:
#     debian/lib.sh: embedded_list_elf_objects + readelf -d
# to see what the tree actually needs before trimming.
#
# For reference, the curated Qt set at the point this closure was introduced --
# 17 `cp -d` lines in create-embedded.sh, which remain the authoritative copy
# unless someone deletes them:
#
#     libQt6Core          libQt6Gui           libQt6DBus (linuxfb plugin)
#     libQt6Quick         libQt6Qml           libQt6QmlCore
#     libQt6QmlMeta       libQt6Network       libQt6Svg
#     libQt6QuickTemplates2                   libQt6QuickLayouts
#     libQt6QuickDialogs2                     libQt6LabsFolderListModel
#     libQt6QuickControls2Basic               libQt6QuickControls2Impl
#     libQt6QuickControls2Material            libQt6QuickControls2MaterialStyleImpl
#
# What this closure adds on top is small, because create-embedded.sh now also
# prunes the QML modules the UI never imports. What remains are libraries the
# curated list genuinely forgot:
#
#     libQt6QuickControls2              QtQuick.Controls is imported by ~29 QML
#                                       files; the list had the Basic/Material
#                                       styles but not Controls2 itself
#     libQt6QuickControls2BasicStyleImpl  needed by the Basic style fallback
#     libQt6QmlModels                   NEEDED by libQt6Quick itself
#     libQt6OpenGL                      NEEDED by libQt6Quick itself
#     libQt6QuickDialogs2QuickImpl      needed by the (curated) Dialogs module
#     libQt6QuickDialogs2Utils
#
# So the pre-existing bug was real: libQt6Quick could not have resolved
# libQt6QmlModels or libQt6OpenGL, and the QtQuick.Controls plugin could not
# have resolved libQt6QuickControls2. Do not "fix" a size regression by
# dropping these -- prune QML modules instead, which removes both the plugin
# and its library together.
embedded_deploy_lib_closure() {
	_optdir=$1
	echo "Completing embedded library closure (triplet: $(appimage_multiarch_triplet || echo unknown))..."
	deploy_lib_closure_core "$_optdir/lib" embedded_list_elf_objects \
		"$_optdir" embedded_lib_excluded "$(deploy_lib_search_dirs "${2:-}")"
}

# Prune the deployed Qt QML tree, its style libraries and the QML tooling down
# to what the UI actually imports. Shared by the AppImage and embedded packaging
# paths so both ship the same QML surface -- they deploy Qt differently
# (linuxdeploy/manual copy vs a curated /opt tree) but there is no reason for
# them to disagree about which modules the app needs.
#
# Imager's QML imports, and nothing more, are:
#   QtQuick, QtQuick.Controls, QtQuick.Controls.Material, QtQuick.Layouts,
#   QtQuick.Window, QtCore, QtQml, Qt.labs.folderlistmodel
# Re-derive with:
#   grep -rhoE '^\s*import\s+[A-Za-z0-9_.]+' --include='*.qml' src/ | sort -u
#
# Every module removed here also removes the need to ship the Qt library its
# plugin links against (QtQuick/Effects -> libQt6QuickEffects, and so on), so
# prune modules rather than trimming libraries by hand: dropping a library
# whose plugin is still deployed leaves the plugin unable to load.
#
# $1 qml dir, $2 lib dir, $3 plugins dir
prune_qml_to_imports() {
	_qmldir=$1
	_libdir=$2
	_plugindir=$3

	# Control styles: Material is the one in use, Basic is the fallback.
	for _style in Universal Fusion Imagine FluentWinUI3; do
		rm -rf "$_qmldir/QtQuick/Controls/$_style"
		rm -f "$_libdir/libQt6QuickControls2$_style.so"*
		rm -f "$_libdir/libQt6QuickControls2${_style}StyleImpl.so"*
	done
	rm -f "$_libdir/libQt6QuickControls2WindowsStyleImpl.so"*

	# Modules the UI never imports.
	for _mod in QtQuick/Effects QtQuick/Particles QtQuick/Shapes \
		QtQuick/Timeline QtQuick/VectorImage \
		QtQml/WorkerScript QtQml/XmlListModel
	do
		rm -rf "$_qmldir/$_mod"
	done

	# ...and the libraries only those modules' plugins linked against. The
	# AppImage path copies libQt6*.so* wholesale, so removing the module alone
	# leaves the library behind; the embedded path never copies them, so these
	# are no-ops there. Verified to have no remaining referrer with:
	#   readelf -d <every surviving object> | grep libQt6<name>
	#
	# libQt6QmlWorkerScript is deliberately NOT in this list: libQt6QmlMeta
	# links it directly, so it is required even though QtQml/WorkerScript is
	# not imported. Re-run the check above before adding anything here.
	for _lib in QuickEffects QuickParticles \
		QuickShapes QuickShapesDesignHelpers \
		QuickTimeline QuickTimelineBlendTrees \
		QuickVectorImage QuickVectorImageGenerator QuickVectorImageHelpers \
		QmlXmlListModel
	do
		rm -f "$_libdir/libQt6$_lib.so"*
	done

	# Development-only tooling and test modules.
	rm -rf "$_qmldir/QtTest"*
	rm -rf "$_qmldir/QtQuick/tooling"
	[ -n "$_plugindir" ] && rm -rf "$_plugindir/qmltooling"

	# QtWidgets is not used by the QML UI.
	rm -f "$_libdir/libQt6Widgets.so"*
	rm -f "$_libdir"/libQt*Widgets.so*

	return 0
}

# Drop unversioned *.so development symlinks copied in from the build system.
# Only versioned sonames are used at runtime, and these links point at absolute
# host paths, so they ship as dangling files.
prune_dev_symlinks() {
	_libdir=$1
	for _l in "$_libdir"/*.so; do
		[ -L "$_l" ] || continue
		case "$(readlink "$_l")" in
		/*) rm -f "$_l"; echo "  pruned dev symlink $(basename "$_l")" ;;
		esac
	done
}

# shellcheck disable=SC1091
. "$TOP/debian/chroot-lib.sh"
