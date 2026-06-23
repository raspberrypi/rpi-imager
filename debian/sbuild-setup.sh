#!/bin/sh
# Optional one-time sbuild/schroot setup for isolated builds.
#
# Reads debian/release.conf when present so paths match release.sh.
# Creates chroots for arm64 and amd64, provisions build deps, and bind-mounts
# the AppImage and Qt caches at the same absolute paths inside chroots.
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
QT_CACHE=${QT_CACHE:-.debian/qt}
QT_CACHE=$(resolve_repo_path "$QT_CACHE")
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

install -d -m 0755 "$OUTPUT_DIR" \
	"$APPIMAGE_ROOT/arm64" "$APPIMAGE_ROOT/amd64" "$APPIMAGE_ROOT/armhf" \
	"$QT_CACHE/arm64" "$QT_CACHE/amd64" "$QT_CACHE/armhf"
chown -R "$SBUILD_USER:$SBUILD_USER" "$OUTPUT_DIR" "$APPIMAGE_ROOT" "$QT_CACHE"

# Bind-mount release caches at identical absolute paths inside chroots.
FSTAB=/etc/schroot/default/fstab
bind_fstab() {
	_path=$1
	_label=$2
	if ! grep -q "$_path" "$FSTAB" 2>/dev/null; then
		cat >>"$FSTAB" <<EOF

# rpi-imager $_label (host -> chroot)
$_path $_path none rw,bind 0 0
EOF
	fi
}

bind_fstab "$APPIMAGE_ROOT" "AppImage cache"
bind_fstab "$QT_CACHE" "Qt cache"

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

for arch in arm64 amd64; do
	create_chroot "$arch"
	"$TOP/debian/sbuild-provision-chroot.sh" "$arch"
done

sbuild-adduser "$SBUILD_USER"

cat <<EOF
sbuild-setup: done.

Configured paths (from debian/release.conf or defaults):
  OUTPUT_DIR=$OUTPUT_DIR
  APPIMAGE_ROOT=$APPIMAGE_ROOT
  QT_CACHE=$QT_CACHE

Qt is built on first AppImage build (debian/ensure-qt.sh) and cached per arch.

Next steps (as ${SBUILD_USER}):
  debian/release.sh status
  debian/release.sh source
  debian/release.sh arch amd64
  debian/release.sh arch arm64

EOF
