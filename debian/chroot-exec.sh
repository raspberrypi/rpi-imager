#!/bin/bash
# Run a command inside an mmdebstrap rootfs (rootless via user namespaces).
#
# Usage:
#   debian/chroot-exec.sh <arch> <command> [args...]
set -euo pipefail

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
# shellcheck disable=SC1091
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: chroot-exec.sh <arch> <command> [args...]}"
shift

if [ "$(chroot_backend_for "$ARCH")" != mmdebstrap ]; then
	echo "chroot-exec: mmdebstrap root missing for $ARCH" >&2
	exit 1
fi

ROOT=$(chroot_mmdebstrap_root "$ARCH")
NAME=$(chroot_name "$ARCH")
BUILD_JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

_inner=$(mktemp "${TMPDIR:-/tmp}/rpi-imager-chroot-exec.XXXXXX")
trap 'rm -f "$_inner"' EXIT INT HUP TERM

{
	printf '%s\n' '#!/bin/bash'
	printf '%s\n' 'set -euo pipefail'
	printf 'root=%q\n' "$ROOT"
	printf 'TOP=%q\n' "$TOP"
	printf 'QT_CACHE=%q\n' "$QT_CACHE"
	printf 'APPIMAGE_ROOT=%q\n' "$APPIMAGE_ROOT"
	printf 'QT_VERSION=%q\n' "$QT_VERSION"
	printf 'NAME=%q\n' "$NAME"
	printf 'CMAKE_BUILD_PARALLEL_LEVEL=%q\n' "$BUILD_JOBS"
	cat <<'EOF'
chroot_mount_pseudo() {
	local _root=$1
	mkdir -p "$_root/dev" "$_root/proc" "$_root/sys" "$_root/run"
	if ! mountpoint -q "$_root/dev" 2>/dev/null; then
		mount --rbind /dev "$_root/dev"
		mount --make-rslave "$_root/dev"
	fi
	if ! mountpoint -q "$_root/proc" 2>/dev/null; then
		mount --rbind /proc "$_root/proc"
		mount --make-rslave "$_root/proc"
	fi
	if ! mountpoint -q "$_root/sys" 2>/dev/null; then
		mount --rbind /sys "$_root/sys"
		mount --make-rslave "$_root/sys"
	fi
}

chroot_umount_all() {
	local _root=$1
	for _bind in "$TOP" "$QT_CACHE" "$APPIMAGE_ROOT"; do
		[ -d "$_bind" ] || continue
		if mountpoint -q "$_root$_bind" 2>/dev/null; then
			umount "$_root$_bind" || true
		fi
	done
	for _mp in "$_root/run" "$_root/sys" "$_root/proc" "$_root/dev"; do
		if mountpoint -q "$_mp" 2>/dev/null; then
			umount -l "$_mp" 2>/dev/null || umount -R "$_mp" 2>/dev/null || true
		fi
	done
}

chroot_mount_pseudo "$root"
trap 'chroot_umount_all "$root"' EXIT INT HUP TERM

for _bind in "$TOP" "$QT_CACHE" "$APPIMAGE_ROOT"; do
	[ -d "$_bind" ] || continue
	mkdir -p "$root$_bind"
	if ! mountpoint -q "$root$_bind" 2>/dev/null; then
		mount --bind "$_bind" "$root$_bind"
	fi
done
chroot "$root" env \
	RPI_IMAGER_CHROOT=1 \
	QT_CACHE="$QT_CACHE" \
	QT_VERSION="$QT_VERSION" \
	APPIMAGE_ROOT="$APPIMAGE_ROOT" \
	TOP="$TOP" \
	CMAKE_BUILD_PARALLEL_LEVEL="$CMAKE_BUILD_PARALLEL_LEVEL" \
	"$@"
chown -R "$(id -u):$(id -g)" "$root" 2>/dev/null || true
EOF
} >"$_inner"
chmod 0755 "$_inner"

_run() {
	"$_inner" "$@"
}

case "$(mmdebstrap_run_mode)" in
	unshare)
		unshare --user --map-root-user --mount --fork --kill-child -- "$_inner" "$@"
		;;
	sudo|auto)
		if [ "$(id -u)" -eq 0 ]; then
			_run "$@"
		else
			sudo "$_inner" "$@"
		fi
		;;
	*)
		echo "chroot-exec: unsupported mmdebstrap mode: $(mmdebstrap_run_mode)" >&2
		exit 1
		;;
esac
