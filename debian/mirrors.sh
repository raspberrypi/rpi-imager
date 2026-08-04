#!/bin/sh
# mmdebstrap mirror selection for Raspberry Pi OS-aligned chroots.
#
# Source after TOP (and optional debian/release.conf) are set:
#   . "$TOP/debian/mirrors.sh"
#
# Apt repository cascade (via preferences.d pinning):
#   armhf  raspbian > rpi > debian
#   arm64  rpi > debian
#   amd64  debian only

DEBIAN_MIRROR=${DEBIAN_MIRROR:-http://deb.debian.org/debian}
RASPBIAN_MIRROR=${RASPBIAN_MIRROR:-http://raspbian.raspberrypi.com/raspbian}
RPI_MIRROR=${RPI_MIRROR:-http://archive.raspberrypi.com/debian}
CHROOT_ARCHES=${CHROOT_ARCHES:-arm64 amd64 armhf}
# Keep in sync with lib.sh; the mmdebstrap hooks run from a staged /tmp tree
# without lib.sh, so this default (and the exported CHROOT_DIST) must match.
CHROOT_DIST=${CHROOT_DIST:-bookworm}

CHROOT_APT_DIR=${CHROOT_APT_DIR:-$TOP/debian/chroot-apt}

# Configure the apt repository cascade in an mmdebstrap rootfs directory.
chroot_configure_apt_dir() {
	arch=$1
	root=$2

	[ -n "$root" ] && [ -d "$root" ] || {
		echo "mirrors: invalid chroot root: $root" >&2
		return 1
	}

	chroot_normalize_apt_list "$root"
	chroot_install_keyrings "$root" "$arch"

	case "$arch" in
		amd64)
			install_apt_sources_into_chroot "$root" \
				"$CHROOT_APT_DIR/debian-${CHROOT_DIST}.sources" debian.sources
			;;
		arm64)
			install_apt_sources_into_chroot "$root" \
				"$CHROOT_APT_DIR/debian-${CHROOT_DIST}.sources" debian.sources
			install_apt_sources_into_chroot "$root" \
				"$CHROOT_APT_DIR/rpi-${CHROOT_DIST}.sources" raspberrypi.sources
			install_apt_preferences_into_chroot "$root" \
				"$CHROOT_APT_DIR/preferences-arm64.pref" 10-rpi-imager-cascade.pref
			;;
		armhf)
			install_apt_sources_into_chroot "$root" \
				"$CHROOT_APT_DIR/raspbian-${CHROOT_DIST}.sources" raspbian.sources
			install_apt_sources_into_chroot "$root" \
				"$CHROOT_APT_DIR/rpi-${CHROOT_DIST}.sources" raspberrypi.sources
			install_apt_sources_into_chroot "$root" \
				"$CHROOT_APT_DIR/debian-${CHROOT_DIST}.sources" debian.sources
			install_apt_preferences_into_chroot "$root" \
				"$CHROOT_APT_DIR/preferences-armhf.pref" 10-rpi-imager-cascade.pref
			;;
		*)
			echo "mirrors: unsupported arch for apt configuration: $arch" >&2
			return 1
			;;
	esac
}

# Host-side keyring cache for mmdebstrap (--keyring, unshare namespace — not in chroot).
keyring_cache_dir() {
	_cache=${KEYRING_CACHE:-.debian/archive-keyrings}
	case "$_cache" in
		/*) printf '%s\n' "$_cache" ;;
		*) printf '%s/%s\n' "$TOP" "$_cache" ;;
	esac
}

fetch_archive_keyrings() {
	_which=${1:-all}
	sh "$TOP/debian/fetch-archive-keyrings.sh" "$_which"
}

keyring_abs_path() {
	_file=$1
	_dir=$(CDPATH= cd -- "$(dirname "$_file")" && pwd)
	printf '%s/%s\n' "$_dir" "$(basename "$_file")"
}

# Primary archive key for mmdebstrap (cached under KEYRING_CACHE).
mmdebstrap_keyring_cached() {
	_arch=$1
	_cache=$(keyring_cache_dir)

	install -d "$_cache"
	fetch_archive_keyrings all
	chown -R "$(id -u):$(id -g)" "$_cache" 2>/dev/null || true
	chmod -R a+rX "$_cache"

	case "$_arch" in
		armhf) _name=raspbian-archive-keyring.gpg ;;
		arm64|amd64) _name=debian-archive-keyring.gpg ;;
		*)
			echo "mirrors: unsupported arch: $_arch" >&2
			return 1
			;;
	esac

	_file="$_cache/$_name"
	[ -f "$_file" ] || {
		echo "mirrors: missing keyring $_file (run fetch-archive-keyrings.sh)" >&2
		return 1
	}

	keyring_abs_path "$_file"
}

# mmdebstrap --mode=unshare runs hooks and apt as a subuid user that cannot
# traverse $HOME (typically mode 700). Stage scripts and keyrings under /tmp.
mmdebstrap_stage_hook_tree() {
	_dest=$(mktemp -d "${TMPDIR:-/tmp}/rpi-imager-mmdebstrap-hooks.XXXXXX")
	chmod 0755 "$_dest"
	mkdir -p "$_dest/debian"
	for _f in mmdebstrap-setup-hook.sh mmdebstrap-configure-apt.sh chroot-apt-install.sh mirrors.sh; do
		install -m 0755 "$TOP/debian/$_f" "$_dest/debian/$_f"
	done
	cp -a "$TOP/debian/chroot-apt" "$_dest/debian/"
	chmod -R a+rX "$_dest"
	printf '%s\n' "$_dest"
}

mmdebstrap_stage_keyring() {
	_arch=$1
	_cached=$(mmdebstrap_keyring_cached "$_arch") || return 1
	_basename=$(basename "$_cached")
	_stage=$(mktemp -d "${TMPDIR:-/tmp}/rpi-imager-mmdebstrap-keys.XXXXXX")
	chmod 0755 "$_stage"
	install -m 0644 "$_cached" "$_stage/$_basename"
	printf '%s\n' "$_stage/$_basename"
}

# Bootstrap mirror URI passed to mmdebstrap (setup-hook replaces sources.list).
mmdebstrap_bootstrap_uri() {
	_arch=$1
	case "$_arch" in
		armhf)
			_mirror=${RASPBIAN_MIRROR%/}
			case "$_mirror" in
				*/raspbian) ;;
				*) _mirror="${_mirror}/raspbian" ;;
			esac
			printf '%s\n' "$_mirror"
			;;
		arm64|amd64)
			printf '%s\n' "${DEBIAN_MIRROR%/}"
			;;
		*)
			return 1
			;;
	esac
}

install_keyring_into_chroot() {
	_root=$1
	_host_key=$2
	_dest_name=$3
	_cache=$(keyring_cache_dir)
	_cached="$_cache/$_dest_name"

	if [ -f "$_cached" ]; then
		_host_key=$_cached
	elif [ ! -f "$_host_key" ]; then
		return 0
	fi
	install -d "$_root/usr/share/keyrings"
	install -m 0644 "$_host_key" "$_root/usr/share/keyrings/$_dest_name"
}

install_apt_sources_into_chroot() {
	_root=$1
	_src=$2
	_dest=$3

	install -d "$_root/etc/apt/sources.list.d"
	install -m 0644 "$_src" "$_root/etc/apt/sources.list.d/$_dest"
}

install_apt_preferences_into_chroot() {
	_root=$1
	_src=$2
	_dest=$3

	install -d "$_root/etc/apt/preferences.d"
	install -m 0644 "$_src" "$_root/etc/apt/preferences.d/$_dest"
}

chroot_normalize_apt_list() {
	_root=$1
	cat >"$_root/etc/apt/sources.list" <<'EOF'
# Managed by debian/mirrors.sh — repositories live in sources.list.d/
EOF
}

chroot_install_keyrings() {
	_root=$1
	_arch=$2

	install_keyring_into_chroot "$_root" \
		/usr/share/keyrings/debian-archive-keyring.gpg \
		debian-archive-keyring.gpg

	case "$_arch" in
		arm64|armhf)
			install_keyring_into_chroot "$_root" \
				/usr/share/keyrings/raspberrypi-archive-keyring.gpg \
				raspberrypi-archive-keyring.gpg
			;;
	esac

	case "$_arch" in
		armhf)
			install_keyring_into_chroot "$_root" \
				/usr/share/keyrings/raspbian-archive-keyring.gpg \
				raspbian-archive-keyring.gpg
			;;
	esac
}
