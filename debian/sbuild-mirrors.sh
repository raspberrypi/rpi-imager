#!/bin/sh
# sbuild/debootstrap mirror selection for Raspberry Pi OS-aligned chroots.
#
# Source after TOP (and optional debian/release.conf) are set:
#   . "$TOP/debian/sbuild-mirrors.sh"
#
# Apt repository cascade (via preferences.d pinning):
#   armhf  raspbian > rpi > debian
#   arm64  rpi > debian
#   amd64  debian only

SBUILD_DEBIAN_MIRROR=${SBUILD_DEBIAN_MIRROR:-http://deb.debian.org/debian}
SBUILD_RASPBIAN_MIRROR=${SBUILD_RASPBIAN_MIRROR:-http://raspbian.raspberrypi.com/raspbian}
SBUILD_RPI_MIRROR=${SBUILD_RPI_MIRROR:-http://archive.raspberrypi.com/debian}
SBUILD_CHROOT_ARCHES=${SBUILD_CHROOT_ARCHES:-arm64 amd64 armhf}

SBUILD_APT_DIR=${SBUILD_APT_DIR:-$TOP/debian/sbuild-apt}

schroot_root() {
	_name=$1
	schroot --info -c "$_name" 2>/dev/null | awk -F': ' '/^[[:space:]]*Directory:/ {print $2; exit}'
}

# Configure apt cascade in a root directory (mmdebstrap or schroot path).
sbuild_configure_apt_dir() {
	arch=$1
	root=$2

	[ -n "$root" ] && [ -d "$root" ] || {
		echo "sbuild-mirrors: invalid chroot root: $root" >&2
		return 1
	}

	sbuild_normalize_apt_list "$root"
	sbuild_install_keyrings "$root" "$arch"

	case "$arch" in
		amd64)
			install_apt_sources_into_chroot "$root" \
				"$SBUILD_APT_DIR/debian-trixie.sources" debian.sources
			;;
		arm64)
			install_apt_sources_into_chroot "$root" \
				"$SBUILD_APT_DIR/debian-trixie.sources" debian.sources
			install_apt_sources_into_chroot "$root" \
				"$SBUILD_APT_DIR/rpi-trixie.sources" raspberrypi.sources
			install_apt_preferences_into_chroot "$root" \
				"$SBUILD_APT_DIR/preferences-arm64.pref" 10-rpi-imager-cascade.pref
			;;
		armhf)
			install_apt_sources_into_chroot "$root" \
				"$SBUILD_APT_DIR/raspbian-trixie.sources" raspbian.sources
			install_apt_sources_into_chroot "$root" \
				"$SBUILD_APT_DIR/rpi-trixie.sources" raspberrypi.sources
			install_apt_sources_into_chroot "$root" \
				"$SBUILD_APT_DIR/debian-trixie.sources" debian.sources
			install_apt_preferences_into_chroot "$root" \
				"$SBUILD_APT_DIR/preferences-armhf.pref" 10-rpi-imager-cascade.pref
			;;
		*)
			echo "sbuild-mirrors: unsupported arch for apt configuration: $arch" >&2
			return 1
			;;
	esac
}

# Configure the full apt repository cascade after debootstrap.
sbuild_configure_apt() {
	arch=$1
	name=$2

	root=$(schroot_root "$name")
	if [ -z "$root" ]; then
		echo "sbuild-mirrors: cannot find schroot root for $name" >&2
		return 1
	fi

	sbuild_configure_apt_dir "$arch" "$root" || return 1

	export DEBIAN_FRONTEND=noninteractive
	schroot -c "$name" -- apt-get update
}

sbuild_debootstrap_mirror() {
	case "$1" in
		armhf) printf '%s\n' "$SBUILD_RASPBIAN_MIRROR" ;;
		arm64|amd64) printf '%s\n' "$SBUILD_DEBIAN_MIRROR" ;;
		*) return 1 ;;
	esac
}

sbuild_debootstrap_keyring() {
	case "$1" in
		armhf)
			if [ -f /usr/share/keyrings/raspbian-archive-keyring.gpg ]; then
				printf '%s\n' /usr/share/keyrings/raspbian-archive-keyring.gpg
			else
				printf '%s\n' /usr/share/keyrings/debian-archive-keyring.gpg
			fi
			;;
		*)
			printf '%s\n' /usr/share/keyrings/debian-archive-keyring.gpg
			;;
	esac
}

sbuild_debootstrap_extra_options() {
	case "$1" in
		armhf) printf '%s\n' --extra-debootstrap-option=--components=main,contrib,non-free,rpi ;;
		arm64|amd64)
			printf '%s\n' --extra-debootstrap-option=--components=main,contrib,non-free,non-free-firmware
			;;
		*) return 1 ;;
	esac
}

sbuild_foreign_debootstrap() {
	_arch=$1
	[ "$_arch" != "$HOST_ARCH" ]
}

install_host_archive_keyrings() {
	# Best-effort: keyrings may only be on the build host's own apt sources.
	apt-get install -y debian-archive-keyring raspbian-archive-keyring raspberrypi-archive-keyring 2>/dev/null || true
}

# Host-side keyring cache for mmdebstrap (--keyring, unshare namespace — not in chroot).
sbuild_keyring_cache_dir() {
	_cache=${KEYRING_CACHE:-.debian/archive-keyrings}
	case "$_cache" in
		/*) printf '%s\n' "$_cache" ;;
		*) printf '%s/%s\n' "$TOP" "$_cache" ;;
	esac
}

sbuild_fetch_archive_keyrings() {
	_which=${1:-all}
	sh "$TOP/debian/fetch-archive-keyrings.sh" "$_which"
}

sbuild_keyring_abs_path() {
	_file=$1
	_dir=$(CDPATH= cd -- "$(dirname "$_file")" && pwd)
	printf '%s/%s\n' "$_dir" "$(basename "$_file")"
}

# Primary archive key for mmdebstrap (cached under KEYRING_CACHE).
sbuild_mmdebstrap_keyring_cached() {
	_arch=$1
	_cache=$(sbuild_keyring_cache_dir)

	install -d "$_cache"
	sbuild_fetch_archive_keyrings all
	chown -R "$(id -u):$(id -g)" "$_cache" 2>/dev/null || true
	chmod -R a+rX "$_cache"

	case "$_arch" in
		armhf) _name=raspbian-archive-keyring.gpg ;;
		arm64|amd64) _name=debian-archive-keyring.gpg ;;
		*)
			echo "sbuild-mirrors: unsupported arch: $_arch" >&2
			return 1
			;;
	esac

	_file="$_cache/$_name"
	[ -f "$_file" ] || {
		echo "sbuild-mirrors: missing keyring $_file (run fetch-archive-keyrings.sh)" >&2
		return 1
	}

	sbuild_keyring_abs_path "$_file"
}

# mmdebstrap --mode=unshare runs apt as a subuid user that cannot traverse
# $HOME (typically mode 700). Copy the keyring to a world-readable /tmp path.
sbuild_mmdebstrap_stage_keyring() {
	_arch=$1
	_cached=$(sbuild_mmdebstrap_keyring_cached "$_arch") || return 1
	_basename=$(basename "$_cached")
	_stage=$(mktemp -d "${TMPDIR:-/tmp}/rpi-imager-mmdebstrap-keys.XXXXXX")
	chmod 0755 "$_stage"
	install -m 0644 "$_cached" "$_stage/$_basename"
	printf '%s\n' "$_stage/$_basename"
}

# One-line apt sources for mmdebstrap (all suites signed-by the bootstrap key).
sbuild_mmdebstrap_write_mirrors() {
	_arch=$1
	_key=$2
	_out=$3
	_suite=$SBUILD_DIST

	case "$_arch" in
		armhf)
			_mirror=${SBUILD_RASPBIAN_MIRROR%/}
			case "$_mirror" in
				*/raspbian) ;;
				*) _mirror="${_mirror}/raspbian" ;;
			esac
			cat >"$_out" <<EOF
deb [signed-by=${_key} arch=armhf] ${_mirror} ${_suite} main contrib non-free rpi
EOF
			;;
		arm64|amd64)
			cat >"$_out" <<EOF
deb [signed-by=${_key} arch=${_arch}] ${SBUILD_DEBIAN_MIRROR%/} ${_suite} main contrib non-free non-free-firmware
deb [signed-by=${_key} arch=${_arch}] ${SBUILD_DEBIAN_MIRROR%/} ${_suite}-updates main contrib non-free non-free-firmware
deb [signed-by=${_key} arch=${_arch}] http://deb.debian.org/debian-security ${_suite}-security main contrib non-free non-free-firmware
EOF
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
	_cache=$(sbuild_keyring_cache_dir)
	_cached="$_cache/$_dest_name"

	if [ ! -f "$_host_key" ] && [ -f "$_cached" ]; then
		_host_key=$_cached
	fi

	[ -f "$_host_key" ] || return 0
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

sbuild_normalize_apt_list() {
	_root=$1
	cat >"$_root/etc/apt/sources.list" <<'EOF'
# Managed by debian/sbuild-mirrors.sh — repositories live in sources.list.d/
EOF
}

sbuild_install_keyrings() {
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
