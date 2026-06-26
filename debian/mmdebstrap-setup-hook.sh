#!/bin/sh
# mmdebstrap --setup-hook: install archive keyring + apt sources inside $1.
#
# Apt during bootstrap uses Dir=$1; Signed-By must reference keyrings under
# that root, not host paths under $HOME or /tmp.
#
# Usage (from mmdebstrap):
#   sh debian/mmdebstrap-setup-hook.sh <rootdir> <arch> <host-keyring-file>
set -eu

ROOT=${1:?rootdir required}
ARCH=${2:?arch required}
KEY_SRC=${3:?host keyring file required}

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

SBUILD_DIST=${SBUILD_DIST:-trixie}
SBUILD_DEBIAN_MIRROR=${SBUILD_DEBIAN_MIRROR:-http://deb.debian.org/debian}
SBUILD_RASPBIAN_MIRROR=${SBUILD_RASPBIAN_MIRROR:-http://raspbian.raspberrypi.com/raspbian}

case "$ARCH" in
	armhf)
		_key_name=raspbian-archive-keyring.gpg
		_key="$ROOT/usr/share/keyrings/$_key_name"
		_mirror=${SBUILD_RASPBIAN_MIRROR%/}
		case "$_mirror" in
			*/raspbian) ;;
			*) _mirror="${_mirror}/raspbian" ;;
		esac
		install -d "$ROOT/usr/share/keyrings" "$ROOT/etc/apt/sources.list.d"
		install -m 0644 "$KEY_SRC" "$_key"
		cat >"$ROOT/etc/apt/sources.list" <<EOF
# Managed by debian/mmdebstrap-setup-hook.sh
EOF
		cat >"$ROOT/etc/apt/sources.list.d/bootstrap.sources" <<EOF
Types: deb
URIs: ${_mirror}
Suites: ${SBUILD_DIST}
Components: main contrib non-free rpi
Architectures: armhf
Signed-By: ${_key}
EOF
		;;
	arm64|amd64)
		_key_name=debian-archive-keyring.gpg
		_key="$ROOT/usr/share/keyrings/$_key_name"
		install -d "$ROOT/usr/share/keyrings" "$ROOT/etc/apt/sources.list.d"
		install -m 0644 "$KEY_SRC" "$_key"
		cat >"$ROOT/etc/apt/sources.list" <<EOF
# Managed by debian/mmdebstrap-setup-hook.sh
EOF
		cat >"$ROOT/etc/apt/sources.list.d/bootstrap.sources" <<EOF
Types: deb
URIs: ${SBUILD_DEBIAN_MIRROR%/}
Suites: ${SBUILD_DIST} ${SBUILD_DIST}-updates
Components: main contrib non-free non-free-firmware
Architectures: ${ARCH}
Signed-By: ${_key}

Types: deb
URIs: http://deb.debian.org/debian-security
Suites: ${SBUILD_DIST}-security
Components: main contrib non-free non-free-firmware
Architectures: ${ARCH}
Signed-By: ${_key}
EOF
		;;
	*)
		echo "mmdebstrap-setup-hook: unsupported arch: $ARCH" >&2
		exit 1
		;;
esac
