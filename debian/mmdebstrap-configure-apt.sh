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

. "$TOP/debian/mirrors.sh"

rm -f "$ROOT/etc/apt/sources.list.d/bootstrap.sources"
case "$ARCH" in
	arm64|armhf)
		# archive.raspberrypi.com keys still use SHA1 certifications; trixie
		# sqv rejects those after 2026-02-01 unless policy is extended.
		install -d "$ROOT/etc/crypto-policies/back-ends"
		cat >"$ROOT/etc/crypto-policies/back-ends/apt-sequoia.config" <<'EOF'
[hash_algorithms]
sha1.second_preimage_resistance = 2030-01-01
EOF
		;;
esac
chroot_configure_apt_dir "$ARCH" "$ROOT"
