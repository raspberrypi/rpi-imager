#!/bin/sh
# Create a rootless cross-build chroot with mmdebstrap (no sudo required).
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

if chroot_mmdebstrap_ok "$ARCH"; then
	echo "mmdebstrap-ensure: $NAME already exists at $ROOT"
	exit 0
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
	armhf)
		_MIRROR=$SBUILD_RASPBIAN_MIRROR
		_COMPONENTS=main,contrib,non-free,rpi
		;;
	arm64|amd64)
		_MIRROR=$SBUILD_DEBIAN_MIRROR
		_COMPONENTS=main,contrib,non-free,non-free-firmware
		;;
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

echo "mmdebstrap-ensure: creating $NAME at $ROOT (mode=$MODE, mirror=$_MIRROR)..."

# shellcheck disable=SC2086
mmdebstrap --mode="$MODE" --variant=minbase \
	--arch="$ARCH" \
	--components="$_COMPONENTS" \
	--include="$_INCLUDE" \
	${_SKIP_QEMU:+--skip="$_SKIP_QEMU"} \
	--customize-hook="sh '$TOP/debian/mmdebstrap-configure-apt.sh' \"\$1\" '$ARCH'" \
	--customize-hook="chroot \"\$1\" apt-get update" \
	--customize-hook="chroot \"\$1\" apt-get install -y $(tr '\n' ' ' <"$TOP/debian/sbuild-chroot-packages")" \
	--customize-hook="touch \"\$1/.rpi-imager-chroot-ok\"" \
	"$SBUILD_DIST" "$ROOT" "$_MIRROR"

echo "mmdebstrap-ensure: $NAME ready at $ROOT"
