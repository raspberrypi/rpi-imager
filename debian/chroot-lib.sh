#!/bin/sh
# Rootless chroot helpers: mmdebstrap chroots under CHROOT_ROOT (no sudo).
#
# Source after debian/lib.sh has set TOP and paths.

if [ -n "${RPI_IMAGER_CHROOT_LIB_LOADED:-}" ]; then
	return 0 2>/dev/null || exit 0
fi
RPI_IMAGER_CHROOT_LIB_LOADED=1

CHROOT_ROOT=${CHROOT_ROOT:-.debian/chroots}
CHROOT_ROOT=$(resolve_repo_path "$CHROOT_ROOT")
MMDEBSTRAP_MODE=${MMDEBSTRAP_MODE:-auto}

chroot_mmdebstrap_root() {
	_arch=$1
	printf '%s/%s\n' "$CHROOT_ROOT" "$(chroot_name "$_arch")"
}

chroot_mmdebstrap_ok() {
	_arch=$1
	_root=$(chroot_mmdebstrap_root "$_arch")
	[ -f "$_root/.rpi-imager-chroot-ok" ] && [ -x "$_root/usr/bin/dpkg" ]
}

chroot_rm_mmdebstrap() {
	_arch=$1
	_root=$(chroot_mmdebstrap_root "$_arch")
	sh "$TOP/debian/chroot-rm.sh" --path "$_root"
	rm -f "${_root}.bootstrap.tar"
}

have_chroot() {
	chroot_mmdebstrap_ok "$1"
}

chroot_backend_for() {
	_arch=$1
	if chroot_mmdebstrap_ok "$_arch"; then
		printf '%s\n' mmdebstrap
	else
		printf '%s\n' none
	fi
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

	case "${CHROOT_AUTO_CREATE:-auto}" in
		0|no|never|false|disabled)
			echo "release: missing chroot $(chroot_name "$_arch") (CHROOT_AUTO_CREATE disabled)" >&2
			return 1
			;;
	esac

	ensure_chroot_dirs
	run_mmdebstrap_ensure_chroot "$_arch" || return 1

	if ! have_chroot "$_arch"; then
		echo "release: failed to create chroot $(chroot_name "$_arch")" >&2
		return 1
	fi
}

ensure_release_chroots() {
	if [ "$#" -eq 0 ]; then
		return 0
	fi

	case "${CHROOT_AUTO_CREATE:-auto}" in
		0|no|never|false|disabled)
			echo "release: cross-arch builds require a chroot or APPIMAGE_REMOTE for:$*" >&2
			for _arch in "$@"; do
				echo "release:   missing: $(chroot_name "$_arch")  (or APPIMAGE_REMOTE_${_arch})" >&2
			done
			echo "release: run: debian/mmdebstrap-ensure-chroot.sh <arch>  or set CHROOT_AUTO_CREATE=auto" >&2
			return 1
			;;
	esac

	ensure_chroot_dirs

	for _arch in "$@"; do
		ensure_chroot "$_arch" || return 1
	done
}

chroot_run() {
	_arch=$1
	shift

	if chroot_mmdebstrap_ok "$_arch"; then
		bash "$TOP/debian/chroot-exec.sh" "$_arch" "$@"
	else
		echo "chroot-run: no chroot for $_arch" >&2
		return 1
	fi
}
