#!/bin/sh
# Remove mmdebstrap chroot trees (handles root-owned files from unshare mode).
#
# Usage:
#   debian/chroot-rm.sh <arm64|amd64|armhf>
#   debian/chroot-rm.sh --path /path/to/chroot
#   debian/chroot-rm.sh --all
set -eu

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

. "$TOP/debian/lib.sh"

chroot_rm_path() {
	_path=$1

	[ -e "$_path" ] || return 0

	if rm -rf "$_path" 2>/dev/null; then
		return 0
	fi

	echo "chroot-rm: normal rm failed for $_path; trying unshare..." >&2
	if command -v unshare >/dev/null 2>&1; then
		if unshare --user --map-root-user rm -rf "$_path"; then
			return 0
		fi
	fi

	if command -v fakeroot >/dev/null 2>&1; then
		echo "chroot-rm: trying fakeroot rm..." >&2
		if fakeroot rm -rf "$_path"; then
			return 0
		fi
	fi

	echo "chroot-rm: could not remove $_path (try: sudo rm -rf '$_path')" >&2
	return 1
}

case "${1:-}" in
	--all)
		chroot_rm_path "$CHROOT_ROOT"
		_cache=${KEYRING_CACHE:-.debian/archive-keyrings}
		case "$_cache" in
			/*) ;;
			*) _cache="$TOP/$_cache" ;;
		esac
		chroot_rm_path "$_cache"
		;;
	--path)
		chroot_rm_path "${2:?usage: chroot-rm.sh --path <directory>}"
		;;
	arm64|amd64|armhf)
		chroot_rm_path "$(chroot_mmdebstrap_root "$1")"
		;;
	-h|--help|help|"")
		cat <<EOF
Usage: debian/chroot-rm.sh <arm64|amd64|armhf|--path DIR|--all>

mmdebstrap --mode=unshare leaves root-owned files on disk. This script
removes them without sudo when unshare --map-root-user is available.
EOF
		;;
	*)
		echo "chroot-rm: unknown argument: $1" >&2
		exit 1
		;;
esac
