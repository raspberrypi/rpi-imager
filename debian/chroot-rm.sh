#!/bin/sh
# Remove mmdebstrap chroot trees (handles subuid/root-owned files from unshare mode).
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

subuid_ids() {
	_user=$(id -un)
	_uid=$(awk -F: -v u="$_user" '$1 == u { print $2; exit }' /etc/subuid 2>/dev/null)
	_gid=$(awk -F: -v u="$_user" '$1 == u { print $2; exit }' /etc/subgid 2>/dev/null)
	[ -n "$_uid" ] && [ -n "$_gid" ] || return 1
	printf '%s %s\n' "$_uid" "$_gid"
}

chroot_rm_mmdebstrap_teardown() {
	_path=$1

	command -v mmdebstrap >/dev/null 2>&1 || return 1
	_mode=$(mmdebstrap_run_mode)

	echo "chroot-rm: mmdebstrap teardown (mode=$_mode) for $_path" >&2
	mmdebstrap --mode="$_mode" --variant=custom \
		--skip=check/empty,setup,update,download,extract,essential,configure,cleanup \
		--customize-hook='rm -rf "$1"' \
		"$SBUILD_DIST" "$_path" "$SBUILD_DEBIAN_MIRROR"
}

chroot_rm_path() {
	_path=$1

	[ -e "$_path" ] || return 0

	if rm -rf "$_path" 2>/dev/null; then
		return 0
	fi

	if _ids=$(subuid_ids 2>/dev/null); then
		# shellcheck disable=SC2086
		set -- $_ids
		_subuid=$1
		_subgid=$2
		if command -v setpriv >/dev/null 2>&1; then
			echo "chroot-rm: trying setpriv --reuid=$_subuid for $_path" >&2
			if setpriv --reuid="$_subuid" --regid="$_subgid" --clear-groups -- \
				rm -rf "$_path" 2>/dev/null; then
				return 0
			fi
		fi
	fi

	if chroot_rm_mmdebstrap_teardown "$_path" 2>/dev/null; then
		[ -e "$_path" ] || return 0
	fi

	echo "chroot-rm: trying unshare --map-root-user for $_path" >&2
	if command -v unshare >/dev/null 2>&1; then
		if unshare --user --map-root-user rm -rf "$_path" 2>/dev/null; then
			return 0
		fi
	fi

	if command -v fakeroot >/dev/null 2>&1; then
		echo "chroot-rm: trying fakeroot rm for $_path" >&2
		if fakeroot rm -rf "$_path" 2>/dev/null; then
			return 0
		fi
	fi

	if [ -e "$_path" ]; then
		echo "chroot-rm: could not remove $_path (try: sudo rm -rf '$_path')" >&2
		return 1
	fi
}

chroot_rm_tree() {
	_path=$1

	if [ ! -d "$_path" ]; then
		chroot_rm_path "$_path"
		return $?
	fi

	_failed=0
	for _child in "$_path"/*; do
		[ -e "$_child" ] || continue
		chroot_rm_path "$_child" || _failed=1
	done

	if [ "$_failed" -eq 0 ]; then
		rmdir "$_path" 2>/dev/null || chroot_rm_path "$_path" || _failed=1
	fi
	return "$_failed"
}

case "${1:-}" in
	--all)
		chroot_rm_tree "$CHROOT_ROOT"
		_cache=${KEYRING_CACHE:-.debian/archive-keyrings}
		case "$_cache" in
			/*) ;;
			*) _cache="$TOP/$_cache" ;;
		esac
		chroot_rm_tree "$_cache"
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

mmdebstrap --mode=unshare leaves files owned by your subuid-mapped root.
This script removes them using setpriv, mmdebstrap teardown, or unshare.
EOF
		;;
	*)
		echo "chroot-rm: unknown argument: $1" >&2
		exit 1
		;;
esac
