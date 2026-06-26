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

chroot_rm_mmdebstrap_teardown() {
	_path=$1
	_owner=$(id -u):$(id -g)

	command -v mmdebstrap >/dev/null 2>&1 || return 1

	for _mode in unshare "$(mmdebstrap_run_mode)"; do
		[ -n "$_mode" ] || continue
		echo "chroot-rm: mmdebstrap teardown (mode=$_mode) for $_path" >&2
		if mmdebstrap --mode="$_mode" --variant=custom \
			--skip=check/empty,setup,update,download,extract,essential,configure,cleanup \
			--customize-hook="chown -R ${_owner} \"\$1\" 2>/dev/null || true" \
			--customize-hook='chmod -R u+rwX "$1" 2>/dev/null || true' \
			--customize-hook='rm -rf "$1"' \
			"$SBUILD_DIST" "$_path" "$SBUILD_DEBIAN_MIRROR"; then
			[ -e "$_path" ] || return 0
		fi
	done
	return 1
}

chroot_rm_with_uids() {
	_path=$1

	command -v setpriv >/dev/null 2>&1 || return 1

	for _uid in $(find "$_path" -mindepth 1 -printf '%u\n' 2>/dev/null | sort -nu); do
		_gid=$_uid
		echo "chroot-rm: trying setpriv --reuid=$_uid for $_path" >&2
		if setpriv --reuid="$_uid" --regid="$_gid" --clear-groups -- \
			rm -rf "$_path" 2>/dev/null; then
			return 0
		fi
	done
	return 1
}

chroot_rm_clear_immutable() {
	_path=$1
	command -v lsattr >/dev/null 2>&1 && command -v chattr >/dev/null 2>&1 || return 1
	if find "$_path" -exec lsattr -d {} + 2>/dev/null | grep -q '[i]'; then
		echo "chroot-rm: clearing immutable flag on $_path" >&2
		chattr -R -i "$_path" 2>/dev/null || true
	fi
}

chroot_rm_path() {
	_path=$1

	[ -e "$_path" ] || return 0

	chroot_rm_clear_immutable "$_path"

	if rm -rf "$_path" 2>/dev/null; then
		return 0
	fi

	chroot_rm_mmdebstrap_teardown "$_path" || true
	[ -e "$_path" ] || return 0

	chroot_rm_clear_immutable "$_path"
	if rm -rf "$_path" 2>/dev/null; then
		return 0
	fi

	chroot_rm_with_uids "$_path" || true
	[ -e "$_path" ] || return 0

	if command -v unshare >/dev/null 2>&1; then
		echo "chroot-rm: trying unshare --map-root-user for $_path" >&2
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
		echo "chroot-rm: could not remove $_path" >&2
		echo "chroot-rm: file owners: $(find "$_path" -mindepth 1 -printf '%u ' 2>/dev/null | tr ' ' '\n' | sort -nu | tr '\n' ' ')" >&2
		echo "chroot-rm: ask an admin to run: sudo rm -rf '$_path'" >&2
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

Removes mmdebstrap chroots, including subuid-owned trees from unshare mode.
EOF
		;;
	*)
		echo "chroot-rm: unknown argument: $1" >&2
		exit 1
		;;
esac
