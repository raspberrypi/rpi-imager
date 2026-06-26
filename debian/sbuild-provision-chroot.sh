#!/bin/sh
# Install AppImage/Qt build dependencies inside a build chroot.
#
# Usage:
#   debian/sbuild-provision-chroot.sh <arm64|amd64|armhf>
set -eu

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: sbuild-provision-chroot.sh <arm64|amd64|armhf>}"
CHROOT="${SBUILD_DIST}-${ARCH}-${SBUILD_CHROOT_SUFFIX}"

if ! have_chroot "$ARCH"; then
	echo "sbuild-provision: chroot ${CHROOT} not found; run debian/mmdebstrap-ensure-chroot.sh or debian/sbuild-setup.sh" >&2
	exit 1
fi

export DEBIAN_FRONTEND=noninteractive
echo "sbuild-provision: ensuring apt repository cascade in ${CHROOT}..."

case "$(chroot_backend_for "$ARCH")" in
	schroot)
		sbuild_configure_apt "$ARCH" "$CHROOT"
		schroot -c "$CHROOT" -- bash -c \
			"apt-get update && apt-get install -y $(tr '\n' ' ' <"$TOP/debian/sbuild-chroot-packages")"
		;;
	mmdebstrap)
		sh "$TOP/debian/mmdebstrap-configure-apt.sh" "$(chroot_mmdebstrap_root "$ARCH")" "$ARCH"
		chroot_run "$ARCH" bash -c \
			"apt-get update && apt-get install -y $(tr '\n' ' ' <"$TOP/debian/sbuild-chroot-packages")"
		;;
	*)
		echo "sbuild-provision: no chroot backend for $ARCH" >&2
		exit 1
		;;
esac

echo "sbuild-provision: ${CHROOT} ready for Qt/AppImage builds"
