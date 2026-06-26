#!/bin/sh
# Create a rootless cross-build chroot with mmdebstrap (no sudo required).
#
# Bootstrap writes a tarball in unshare mode, then extracts it as the current
# user so the tree is always removable without sudo.
#
# Usage:
#   debian/mmdebstrap-ensure-chroot.sh <arm64|amd64|armhf>
#
# Chroots live under CHROOT_ROOT (default: .debian/chroots/).
set -eu

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

. "$TOP/debian/lib.sh"

ARCH="${1:?usage: mmdebstrap-ensure-chroot.sh <arm64|amd64|armhf>}"
NAME=$(chroot_name "$ARCH")
ROOT=$(chroot_mmdebstrap_root "$ARCH")
MODE=$(mmdebstrap_run_mode)
_TAR="${ROOT}.bootstrap.tar"
_OWNER_UID=$(id -u)
_OWNER_GID=$(id -g)

if chroot_mmdebstrap_ok "$ARCH"; then
	echo "mmdebstrap-ensure: $NAME already exists at $ROOT"
	exit 0
fi

if [ -d "$ROOT" ] || [ -f "$_TAR" ]; then
	echo "mmdebstrap-ensure: removing incomplete $NAME..."
	chroot_rm_mmdebstrap "$ARCH" || true
	rm -f "$_TAR"
fi

if ! command -v mmdebstrap >/dev/null 2>&1; then
	echo "mmdebstrap-ensure: install mmdebstrap (apt install mmdebstrap)" >&2
	exit 1
fi

if [ "$ARCH" != "$HOST_ARCH" ]; then
	for _pkg in qemu-user-static binfmt-support; do
		if ! dpkg -s "$_pkg" >/dev/null 2>&1; then
			echo "mmdebstrap-ensure: install $_pkg on the host for $ARCH builds" >&2
			exit 1
		fi
	done
fi

ensure_chroot_dirs

case "$ARCH" in
	armhf) _COMPONENTS=main,contrib,non-free,rpi ;;
	arm64|amd64) _COMPONENTS=main,contrib,non-free,non-free-firmware ;;
	*)
		echo "mmdebstrap-ensure: unsupported arch: $ARCH" >&2
		exit 1
		;;
esac

_INCLUDE=git,ca-certificates
if [ "$ARCH" != "$HOST_ARCH" ]; then
	_INCLUDE="$_INCLUDE,qemu-user-static,binfmt-support"
fi

_SKIP_QEMU=
if [ "$ARCH" != "$HOST_ARCH" ] && ! command -v arch-test >/dev/null 2>&1; then
	_SKIP_QEMU=check/qemu
fi

. "$TOP/debian/sbuild-mirrors.sh"

_KEYRING=$(sbuild_mmdebstrap_stage_keyring "$ARCH") || exit 1
_KEYRING_STAGE=$(dirname "$_KEYRING")
_BOOTSTRAP_URI=$(sbuild_mmdebstrap_bootstrap_uri "$ARCH") || exit 1
trap 'rm -rf "$_KEYRING_STAGE"' EXIT INT HUP TERM

echo "mmdebstrap-ensure: creating $NAME (mode=$MODE, tar -> $ROOT)..."
echo "mmdebstrap-ensure: bootstrap keyring: $_KEYRING"
echo "mmdebstrap-ensure: bootstrap mirror: $_BOOTSTRAP_URI"

_cleanup_partial() {
	if [ -f "$_TAR" ]; then
		rm -f "$_TAR"
	fi
	if [ -d "$ROOT" ] && ! chroot_mmdebstrap_ok "$ARCH"; then
		echo "mmdebstrap-ensure: cleaning up failed $NAME at $ROOT..." >&2
		chroot_rm_mmdebstrap "$ARCH" || true
	fi
}
trap _cleanup_partial EXIT INT HUP TERM

# shellcheck disable=SC2086
mmdebstrap --mode="$MODE" --format=tar --variant=minbase \
	--arch="$ARCH" \
	--keyring="$_KEYRING" \
	--aptopt='APT::Sandbox::User "root"' \
	--components="$_COMPONENTS" \
	--include="$_INCLUDE" \
	${_SKIP_QEMU:+--skip="$_SKIP_QEMU"} \
	--setup-hook="sh '$TOP/debian/mmdebstrap-setup-hook.sh' \"\$1\" '$ARCH' '$_KEYRING'" \
	--customize-hook="sh '$TOP/debian/mmdebstrap-configure-apt.sh' \"\$1\" '$ARCH'" \
	--customize-hook="chroot \"\$1\" apt-get update" \
	--customize-hook="chroot \"\$1\" apt-get install -y $(tr '\n' ' ' <"$TOP/debian/sbuild-chroot-packages")" \
	--customize-hook="touch \"\$1/.rpi-imager-chroot-ok\"" \
	"$SBUILD_DIST" "$_TAR" "$_BOOTSTRAP_URI"

rm -rf "$_KEYRING_STAGE"
trap - EXIT INT HUP TERM

rm -rf "$ROOT"
mkdir -p "$ROOT"
tar --no-same-owner --no-same-permissions -xf "$_TAR" -C "$ROOT"
rm -f "$_TAR"
chown -R "$_OWNER_UID:$_OWNER_GID" "$ROOT"

trap - EXIT INT HUP TERM
echo "mmdebstrap-ensure: $NAME ready at $ROOT"
