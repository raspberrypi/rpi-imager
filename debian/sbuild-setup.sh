#!/bin/sh
# One-time sbuild/schroot setup for isolated .deb builds (requires root).
#
# For AppImage/cross-arch builds without sudo, use mmdebstrap instead:
#   debian/mmdebstrap-ensure-chroot.sh arm64 armhf
#
# Creates schroots aligned with Raspberry Pi OS apt archives:
#   armhf  — raspbian > rpi > debian
#   arm64  — rpi > debian
#   amd64  — debian only
#
# Usage (execute — do not source; sourcing runs exit in your shell):
#   sudo debian/sbuild-setup.sh
#
# release.sh also creates mmdebstrap chroots automatically when missing.
set -eu

case "$0" in
	*sbuild-setup.sh) ;;
	*)
		echo "debian/sbuild-setup.sh must be executed, not sourced." >&2
		echo "  sudo debian/sbuild-setup.sh" >&2
		return 1 2>/dev/null || exit 1
		;;
esac

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root: sudo debian/sbuild-setup.sh" >&2
	exit 1
fi

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

SBUILD_CHROOT_ARCHES=${SBUILD_CHROOT_ARCHES:-arm64 amd64 armhf}
SBUILD_USER=${SBUILD_USER:-${SUDO_USER:-$USER}}

sh "$TOP/debian/sbuild-ensure-chroots.sh" $SBUILD_CHROOT_ARCHES

cat <<EOF
sbuild-setup: done.

Chroot repository cascade (apt pinning):
  armhf  Raspbian ($SBUILD_RASPBIAN_MIRROR) > RPi ($SBUILD_RPI_MIRROR) > Debian ($SBUILD_DEBIAN_MIRROR)
  arm64  RPi ($SBUILD_RPI_MIRROR) > Debian ($SBUILD_DEBIAN_MIRROR)
  amd64  Debian ($SBUILD_DEBIAN_MIRROR)

Next steps (as ${SBUILD_USER}):
  debian/release.sh status
  make release

EOF
