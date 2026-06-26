#!/bin/sh
# Chroot helpers: schroot (root setup) or mmdebstrap (rootless under CHROOT_ROOT).
#
# Source after debian/lib.sh has set TOP and paths.

if [ -n "${RPI_IMAGER_CHROOT_LIB_LOADED:-}" ]; then
	return 0 2>/dev/null || exit 0
fi
RPI_IMAGER_CHROOT_LIB_LOADED=1

CHROOT_ROOT=${CHROOT_ROOT:-.debian/chroots}
CHROOT_ROOT=$(resolve_repo_path "$CHROOT_ROOT")
# auto: use schroot when registered, else mmdebstrap; mmdebstrap|schroot to force creation
CHROOT_BACKEND=${CHROOT_BACKEND:-auto}
MMDEBSTRAP_MODE=${MMDEBSTRAP_MODE:-auto}

chroot_mmdebstrap_root() {
	_arch=$1
	printf '%s/%s\n' "$CHROOT_ROOT" "$(chroot_name "$_arch")"
}

chroot_mmdebstrap_ok() {
	_arch=$1
	_root=$(chroot_mmdebstrap_root "$_arch")
	[ -f "$_root/.rpi-imager-chroot-ok" ] && [ -d "$_root/usr/bin/dpkg" ]
}

chroot_rm_mmdebstrap() {
	_arch=$1
	_root=$(chroot_mmdebstrap_root "$_arch")
	sh "$TOP/debian/chroot-rm.sh" --path "$_root"
	rm -f "${_root}.bootstrap.tar"
}

have_schroot_chroot() {
	_arch=$1
	schroot -l 2>/dev/null | grep -q "^$(chroot_name "$_arch")\$"
}

have_chroot() {
	_arch=$1
	if have_schroot_chroot "$_arch"; then
		return 0
	fi
	chroot_mmdebstrap_ok "$_arch"
}

chroot_backend_for() {
	_arch=$1
	if have_schroot_chroot "$_arch"; then
		printf '%s\n' schroot
	elif chroot_mmdebstrap_ok "$_arch"; then
		printf '%s\n' mmdebstrap
	else
		printf '%s\n' none
	fi
}

chroot_root_dir() {
	_arch=$1
	_backend=$(chroot_backend_for "$_arch")
	case "$_backend" in
		schroot)
			schroot --info -c "$(chroot_name "$_arch")" 2>/dev/null \
				| awk -F': ' '/^[[:space:]]*Directory:/ {print $2; exit}'
			;;
		mmdebstrap)
			chroot_mmdebstrap_root "$_arch"
			;;
	esac
}

mmdebstrap_run_mode() {
	case "$MMDEBSTRAP_MODE" in
		auto)
			if [ "$(id -u)" -eq 0 ]; then
				printf '%s\n' auto
			else
				printf '%s\n' unshare
			fi
			;;
		*)
			printf '%s\n' "$MMDEBSTRAP_MODE"
			;;
	esac
}

chroot_create_prefers_mmdebstrap() {
	case "$CHROOT_BACKEND" in
		mmdebstrap) return 0 ;;
		schroot) return 1 ;;
		auto) return 0 ;;
		*) return 0 ;;
	esac
}

chroot_create_prefers_schroot() {
	case "$CHROOT_BACKEND" in
		schroot) return 0 ;;
		mmdebstrap) return 1 ;;
		auto)
			if [ "$(id -u)" -eq 0 ]; then
				return 0
			fi
			if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
				return 0
			fi
			return 1
			;;
		*) return 1 ;;
	esac
}

ensure_chroot_dirs() {
	install -d -m 0755 "$CHROOT_ROOT" \
		"$OUTPUT_DIR" \
		"$APPIMAGE_ROOT/arm64" "$APPIMAGE_ROOT/amd64" "$APPIMAGE_ROOT/armhf" \
		"$QT_CACHE/arm64" "$QT_CACHE/amd64" "$QT_CACHE/armhf"
}

run_mmdebstrap_ensure_chroot() {
	_arch=$1
	if ! command -v mmdebstrap >/dev/null 2>&1; then
		echo "release: mmdebstrap not installed (apt install mmdebstrap)" >&2
		return 1
	fi
	sh "$TOP/debian/mmdebstrap-ensure-chroot.sh" "$_arch"
}

run_sbuild_ensure_chroots() {
	# shellcheck disable=SC2048,SC2086
	_sudo=${1:-}
	shift

	if [ "$#" -eq 0 ]; then
		return 0
	fi

	echo "release: ensuring schroots for:$*"
	if [ -n "$_sudo" ]; then
		"$_sudo" "$TOP/debian/sbuild-ensure-chroots.sh" "$@"
	elif [ "$(id -u)" -eq 0 ]; then
		sh "$TOP/debian/sbuild-ensure-chroots.sh" "$@"
	else
		echo "release: root required for schroot setup; use mmdebstrap or sudo" >&2
		return 1
	fi
}

ensure_chroot() {
	_arch=$1

	if [ "$_arch" = "$HOST_ARCH" ]; then
		return 0
	fi
	if have_chroot "$_arch"; then
		return 0
	fi
	if appimage_remote_host "$_arch" 2>/dev/null; then
		return 0
	fi

	case "${SBUILD_AUTO_CHROOTS:-auto}" in
		0|no|never|false|disabled)
			echo "release: missing chroot $(chroot_name "$_arch") (SBUILD_AUTO_CHROOTS disabled)" >&2
			return 1
			;;
	esac

	ensure_chroot_dirs

	if chroot_create_prefers_mmdebstrap; then
		run_mmdebstrap_ensure_chroot "$_arch" || {
			if ! chroot_create_prefers_schroot; then
				return 1
			fi
		}
	fi

	if ! have_chroot "$_arch" && chroot_create_prefers_schroot; then
		if command -v sudo >/dev/null 2>&1 && [ "$(id -u)" -ne 0 ]; then
			if sudo -n true 2>/dev/null; then
				run_sbuild_ensure_chroots sudo "$_arch" || return 1
			else
				echo "release: sudo password required to create $(chroot_name "$_arch")..."
				run_sbuild_ensure_chroots sudo "$_arch" || return 1
			fi
		else
			run_sbuild_ensure_chroots "" "$_arch" || return 1
		fi
	fi

	if ! have_chroot "$_arch"; then
		echo "release: failed to create chroot $(chroot_name "$_arch")" >&2
		return 1
	fi
}

ensure_release_chroots() {
	if [ "$#" -eq 0 ]; then
		return 0
	fi

	case "${SBUILD_AUTO_CHROOTS:-auto}" in
		0|no|never|false|disabled)
			echo "release: cross-arch builds require a chroot or APPIMAGE_REMOTE for:$*" >&2
			for _arch in "$@"; do
				echo "release:   missing: $(chroot_name "$_arch")  (or APPIMAGE_REMOTE_${_arch})" >&2
			done
			echo "release: run: debian/mmdebstrap-ensure-chroot.sh <arch>  or set SBUILD_AUTO_CHROOTS=auto" >&2
			return 1
			;;
	esac

	ensure_chroot_dirs

	for _arch in "$@"; do
		ensure_chroot "$_arch" || return 1
	done
}

# Backward-compatible alias.
ensure_schroot() {
	ensure_chroot "$@"
}

chroot_bind_mounts() {
	_root=$1
	for _bind in "$TOP" "$QT_CACHE" "$APPIMAGE_ROOT"; do
		[ -d "$_bind" ] || continue
		mkdir -p "$_root$_bind"
		if ! mountpoint -q "$_root$_bind" 2>/dev/null; then
			mount --bind "$_bind" "$_root$_bind" 2>/dev/null || return 1
		fi
	done
	return 0
}

chroot_run() {
	_arch=$1
	shift

	_backend=$(chroot_backend_for "$_arch")
	case "$_backend" in
		schroot)
			schroot -c "$(chroot_name "$_arch")" -- "$@"
			;;
		mmdebstrap)
			sh "$TOP/debian/chroot-exec.sh" "$_arch" "$@"
			;;
		*)
			echo "chroot-run: no chroot for $_arch" >&2
			return 1
			;;
	esac
}
