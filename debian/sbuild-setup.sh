#!/bin/sh
# Optional one-time sbuild/schroot setup for isolated builds.
#
# Reads debian/release.conf when present so paths match release.sh.
# Creates chroots aligned with Raspberry Pi OS apt archives:
#   armhf  — raspbian > rpi > debian
#   arm64  — rpi > debian
#   amd64  — debian only
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

. "$TOP/debian/sbuild-mirrors.sh"

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
HOST_ARCH=$(dpkg --print-architecture)
SBUILD_USER=${SBUILD_USER:-${SUDO_USER:-$USER}}

chroot_name() {
	printf '%s-%s-%s\n' "$SBUILD_DIST" "$1" "$SBUILD_CHROOT_SUFFIX"
}

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y sbuild schroot debootstrap qemu-user-static binfmt-support \
	uidmap git-buildpackage
install_host_archive_keyrings

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
	mirror=$(sbuild_debootstrap_mirror "$arch")
	keyring=$(sbuild_debootstrap_keyring "$arch")
	extra_opt=$(sbuild_debootstrap_extra_options "$arch")

	if schroot -l | grep -q "^${name}\$"; then
		echo "sbuild-setup: chroot ${name} already exists"
		return 0
	fi

	echo "sbuild-setup: creating ${name} (debootstrap mirror: ${mirror})..."
	if sbuild_foreign_debootstrap "$arch"; then
		sbuild-adduser "$SBUILD_USER"
		# shellcheck disable=SC2086
		sbuild-createchroot --arch="$arch" "$name" "$mirror" \
			"--include=git,ca-certificates" \
			$extra_opt \
			"--foreign" \
			"--keyring=$keyring"
		schroot -c "$name" -- bash -c \
			'apt-get update && apt-get install -y qemu-user-static binfmt-support && /debootstrap/debootstrap --second-stage'
	else
		# shellcheck disable=SC2086
		sbuild-createchroot --arch="$arch" "$name" "$mirror" \
			"--include=git,ca-certificates" \
			$extra_opt
	fi

	sbuild_configure_apt "$arch" "$name"
}

for arch in $SBUILD_CHROOT_ARCHES; do
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

Chroot repository cascade (apt pinning):
  armhf  Raspbian ($SBUILD_RASPBIAN_MIRROR) > RPi ($SBUILD_RPI_MIRROR) > Debian ($SBUILD_DEBIAN_MIRROR)
  arm64  RPi ($SBUILD_RPI_MIRROR) > Debian ($SBUILD_DEBIAN_MIRROR)
  amd64  Debian ($SBUILD_DEBIAN_MIRROR)

Qt is built on first AppImage build (debian/ensure-qt.sh) and cached per arch.

Next steps (as ${SBUILD_USER}):
  debian/release.sh status
  debian/release.sh source
  debian/release.sh arch amd64
  debian/release.sh arch arm64
  debian/release.sh arch armhf

EOF
