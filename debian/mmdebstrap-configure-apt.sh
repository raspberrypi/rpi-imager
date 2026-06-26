#!/bin/sh
# Configure Pi-aligned apt sources inside an mmdebstrap rootfs.
#
# Usage: mmdebstrap-configure-apt.sh <rootdir> <arch>
set -eu

ROOT=${1:?root directory required}
ARCH=${2:?arch required}

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

. "$TOP/debian/sbuild-mirrors.sh"

sbuild_configure_apt_dir "$ARCH" "$ROOT"
