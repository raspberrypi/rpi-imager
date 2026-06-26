#!/bin/sh
# Install AppImage/Qt build dependencies inside an sbuild chroot.
#
# Usage (as root):
#   debian/sbuild-provision-chroot.sh <arm64|amd64|armhf>
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root: sudo debian/sbuild-provision-chroot.sh <arch>" >&2
	exit 1
fi

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ARCH="${1:?usage: sbuild-provision-chroot.sh <arm64|amd64|armhf>}"

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

. "$TOP/debian/sbuild-mirrors.sh"

SBUILD_DIST=${SBUILD_DIST:-trixie}
SBUILD_CHROOT_SUFFIX=${SBUILD_CHROOT_SUFFIX:-sbuild}
CHROOT="${SBUILD_DIST}-${ARCH}-${SBUILD_CHROOT_SUFFIX}"

if ! schroot -l | grep -q "^${CHROOT}\$"; then
	echo "sbuild-provision: chroot ${CHROOT} not found; run debian/sbuild-setup.sh first" >&2
	exit 1
fi

export DEBIAN_FRONTEND=noninteractive
echo "sbuild-provision: ensuring apt repository cascade in ${CHROOT}..."
sbuild_configure_apt "$ARCH" "$CHROOT"

echo "sbuild-provision: installing packages in ${CHROOT}..."
schroot -c "$CHROOT" -- bash -c \
	"apt-get update && apt-get install -y $(tr '\n' ' ' <"$TOP/debian/sbuild-chroot-packages")"

echo "sbuild-provision: ${CHROOT} ready for Qt/AppImage builds"
