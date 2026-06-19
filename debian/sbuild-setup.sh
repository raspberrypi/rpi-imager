#!/bin/sh
# Optional one-time sbuild/schroot setup for isolated builds.
#
# Reads debian/release.conf when present so paths match release.sh.
# Creates chroots for the host architecture and, when possible, amd64.
#
# Usage:
#   sudo debian/sbuild-setup.sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root: sudo debian/sbuild-setup.sh" >&2
	exit 1
fi

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

# Load the same config developers use, without requiring lib.sh return handling.
if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

resolve_repo_path() {
	case "$1" in
		/*) printf '%s\n' "$1" ;;
		*) printf '%s/%s\n' "$TOP" "$1" ;;
	esac
}

OUTPUT_DIR=${OUTPUT_DIR:-out/debian}
OUTPUT_DIR=$(resolve_repo_path "$OUTPUT_DIR")
APPIMAGE_ROOT=${APPIMAGE_ROOT:-.debian/appimages}
APPIMAGE_ROOT=$(resolve_repo_path "$APPIMAGE_ROOT")
SBUILD_DIST=${SBUILD_DIST:-trixie}
SBUILD_CHROOT_SUFFIX=${SBUILD_CHROOT_SUFFIX:-sbuild}
MIRROR=${MIRROR:-http://deb.debian.org/debian}
HOST_ARCH=$(dpkg --print-architecture)
SBUILD_USER=${SBUILD_USER:-${SUDO_USER:-$USER}}

chroot_name() {
	printf '%s-%s-%s\n' "$SBUILD_DIST" "$1" "$SBUILD_CHROOT_SUFFIX"
}

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y sbuild schroot debootstrap qemu-user-static binfmt-support \
	uidmap git-buildpackage

install -d -m 0755 "$OUTPUT_DIR" "$APPIMAGE_ROOT/arm64" \
	"$APPIMAGE_ROOT/amd64" "$APPIMAGE_ROOT/armhf"
chown -R "$SBUILD_USER:$SBUILD_USER" "$OUTPUT_DIR" "$APPIMAGE_ROOT"

# Bind-mount the configured AppImage cache at the same absolute path in chroots.
FSTAB=/etc/schroot/default/fstab
if ! grep -q "$APPIMAGE_ROOT" "$FSTAB" 2>/dev/null; then
	cat >>"$FSTAB" <<EOF

# rpi-imager AppImage cache (host -> chroot)
$APPIMAGE_ROOT $APPIMAGE_ROOT none rw,bind 0 0
EOF
fi

create_chroot() {
	arch=$1
	name=$(chroot_name "$arch")

	if schroot -l | grep -q "^${name}\$"; then
		echo "sbuild-setup: chroot ${name} already exists"
		return 0
	fi

	echo "sbuild-setup: creating ${name}..."
	if [ "$arch" = "$HOST_ARCH" ]; then
		sbuild-createchroot --arch="$arch" "$name" "$MIRROR" \
			"--include=git,ca-certificates"
	else
		sbuild-adduser "$SBUILD_USER"
		sbuild-createchroot --arch="$arch" "$name" "$MIRROR" \
			"--include=git,ca-certificates" \
			"--foreign" \
			"--keyring=/usr/share/keyrings/debian-archive-keyring.gpg"
		schroot -c "$name" -- bash -c \
			'apt-get update && apt-get install -y qemu-user-static binfmt-support && /debootstrap/debootstrap --second-stage'
	fi
}

create_chroot "$HOST_ARCH"
if [ "$HOST_ARCH" != amd64 ]; then
	create_chroot amd64
fi

sbuild-adduser "$SBUILD_USER"

cat <<EOF
sbuild-setup: done.

Configured paths (from debian/release.conf or defaults):
  OUTPUT_DIR=$OUTPUT_DIR
  APPIMAGE_ROOT=$APPIMAGE_ROOT

Next steps (as ${SBUILD_USER}):
  debian/release.sh status
  debian/release.sh source
  debian/release.sh arch ${HOST_ARCH} --build

EOF
