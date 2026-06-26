#!/bin/sh
# Create sbuild/schroot environments (idempotent). Run as root.
#
# Usage:
#   sudo debian/sbuild-ensure-chroots.sh              # all SBUILD_CHROOT_ARCHES
#   sudo debian/sbuild-ensure-chroots.sh arm64 armhf  # specific arches only
#
# Called automatically by debian/release.sh when schroots are missing.
#
# Execute only — do not source (exit/return handling and apt-get need a subshell).
set -eu

case "$0" in
	*sbuild-ensure-chroots.sh) ;;
	*)
		echo "debian/sbuild-ensure-chroots.sh: execute this script, do not source it." >&2
		echo "  sudo debian/sbuild-ensure-chroots.sh [arch...]" >&2
		return 1 2>/dev/null || exit 1
		;;
esac

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root: sudo debian/sbuild-ensure-chroots.sh [arch...]" >&2
	exit 1
fi

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

SBUILD_DIST=${SBUILD_DIST:-trixie}
SBUILD_CHROOT_SUFFIX=${SBUILD_CHROOT_SUFFIX:-sbuild}

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
HOST_ARCH=$(dpkg --print-architecture)
SBUILD_USER=${SBUILD_USER:-${SUDO_USER:-${USER:-root}}}

chroot_name() {
	printf '%s-%s-%s\n' "$SBUILD_DIST" "$1" "$SBUILD_CHROOT_SUFFIX"
}

ensure_host_packages() {
	export DEBIAN_FRONTEND=noninteractive
	apt-get update
	apt-get install -y sbuild schroot debootstrap qemu-user-static binfmt-support \
		uidmap git-buildpackage
	install_host_archive_keyrings
}

ensure_cache_dirs() {
	install -d -m 0755 "$OUTPUT_DIR" \
		"$APPIMAGE_ROOT/arm64" "$APPIMAGE_ROOT/amd64" "$APPIMAGE_ROOT/armhf" \
		"$QT_CACHE/arm64" "$QT_CACHE/amd64" "$QT_CACHE/armhf"
	if id "$SBUILD_USER" >/dev/null 2>&1; then
		chown -R "$SBUILD_USER:$SBUILD_USER" "$OUTPUT_DIR" "$APPIMAGE_ROOT" "$QT_CACHE"
	fi
}

ensure_bind_mounts() {
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
	bind_fstab "$TOP" "source tree"
}

create_chroot() {
	arch=$1
	name=$(chroot_name "$arch")
	mirror=$(sbuild_debootstrap_mirror "$arch")
	keyring=$(sbuild_debootstrap_keyring "$arch")
	extra_opt=$(sbuild_debootstrap_extra_options "$arch")

	if schroot -l 2>/dev/null | grep -q "^${name}\$"; then
		echo "sbuild-ensure: chroot ${name} already exists"
		return 0
	fi

	if [ -f "$(resolve_repo_path "${CHROOT_ROOT:-.debian/chroots}")/${name}/.rpi-imager-chroot-ok" ]; then
		echo "sbuild-ensure: mmdebstrap chroot ${name} already exists (skipping schroot create)"
		return 0
	fi

	echo "sbuild-ensure: creating ${name} (debootstrap mirror: ${mirror})..."
	if sbuild_foreign_debootstrap "$arch"; then
		sbuild-adduser "$SBUILD_USER" 2>/dev/null || true
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
	sh "$TOP/debian/sbuild-provision-chroot.sh" "$arch"
}

if [ "$#" -gt 0 ]; then
	TARGET_ARCHES=$*
else
	TARGET_ARCHES=$SBUILD_CHROOT_ARCHES
fi

ensure_host_packages
ensure_cache_dirs
ensure_bind_mounts

for arch in $TARGET_ARCHES; do
	create_chroot "$arch"
done

if id "$SBUILD_USER" >/dev/null 2>&1; then
	sbuild-adduser "$SBUILD_USER" 2>/dev/null || true
fi

echo "sbuild-ensure: ready for:$TARGET_ARCHES"
