#!/bin/sh
# Fetch archive signing keyrings for mmdebstrap (host-side, not inside the chroot).
#
# Keys are cached under KEYRING_CACHE (default: .debian/archive-keyrings/).
# Host /usr/share/keyrings is reused when present; otherwise keys are downloaded
# from the official Debian / Raspbian / Raspberry Pi archives.
#
# Usage:
#   debian/fetch-archive-keyrings.sh [debian|raspbian|raspberrypi|all]
set -eu

TOP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ -f "$TOP/debian/release.conf" ]; then
	# shellcheck disable=SC1091
	. "$TOP/debian/release.conf"
fi

SBUILD_DIST=${SBUILD_DIST:-trixie}
SBUILD_DEBIAN_MIRROR=${SBUILD_DEBIAN_MIRROR:-http://deb.debian.org/debian}
SBUILD_RASPBIAN_MIRROR=${SBUILD_RASPBIAN_MIRROR:-http://raspbian.raspberrypi.com/raspbian}
SBUILD_RPI_MIRROR=${SBUILD_RPI_MIRROR:-http://archive.raspberrypi.com/debian}

KEYRING_CACHE=${KEYRING_CACHE:-.debian/archive-keyrings}
case "$KEYRING_CACHE" in
	/*) ;;
	*) KEYRING_CACHE="$TOP/$KEYRING_CACHE" ;;
esac

TARGET=${1:-all}

need_cmd() {
	command -v "$1" >/dev/null 2>&1 || {
		echo "fetch-archive-keyrings: required command missing: $1" >&2
		exit 1
	}
}

need_cmd curl
need_cmd gzip

install -d "$KEYRING_CACHE"

_packages_filename() {
	_mirror=$1
	_suite=$2
	_pkg=$3
	curl -fsSL "${_mirror%/}/dists/${_suite}/main/binary-all/Packages.gz" \
		| gzip -dc \
		| awk -v pkg="$_pkg" '
			$1 == "Package:" && $2 == pkg { found = 1; next }
			found && $1 == "Package:" { exit 1 }
			found && $1 == "Filename:" { print $2; exit }
		'
}

copy_or_use() {
	_dest=$1
	_src=$2

	if [ ! -f "$_src" ]; then
		return 1
	fi
	if [ "$_src" -ef "$_dest" ]; then
		echo "fetch-archive-keyrings: using $_src"
		return 0
	fi
	install -m 0644 "$_src" "$_dest"
	echo "fetch-archive-keyrings: using $_src"
	return 0
}

_fetch_pool_deb_keyring() {
	_label=$1
	_dest=$2
	_mirror=$3
	_suite=$4
	_pkg=$5
	_fallback=$6

	_file=$(_packages_filename "$_mirror" "$_suite" "$_pkg" 2>/dev/null) || _file=
	if [ -z "$_file" ]; then
		_file=$_fallback
	fi

	_tmp=$(mktemp -d "${TMPDIR:-/tmp}/rpi-imager-keyring.XXXXXX")
	_deb="$_tmp/pkg.deb"
	# shellcheck disable=SC2064
	trap "rm -rf '$_tmp'" EXIT INT HUP TERM

	echo "fetch-archive-keyrings: downloading $_pkg from ${_mirror%/}/$_file"
	curl -fsSL "${_mirror%/}/$_file" -o "$_deb"

	if command -v dpkg-deb >/dev/null 2>&1; then
		dpkg-deb -x "$_deb" "$_tmp/root"
	else
		need_cmd ar
		need_cmd tar
		(
			cd "$_tmp"
			ar x "$_deb" data.tar.*
			tar xf data.tar.*
		)
	fi

	_gpg=$(find "$_tmp/root/usr/share/keyrings" -maxdepth 1 -type f 2>/dev/null | head -1)
	[ -n "$_gpg" ] || {
		echo "fetch-archive-keyrings: no keyring inside $_pkg ($_file)" >&2
		return 1
	}

	install -m 0644 "$_gpg" "$_dest"
	echo "fetch-archive-keyrings: installed $_label -> $_dest"
}

_fetch_ascii_key() {
	_label=$1
	_dest=$2
	_url=$3

	echo "fetch-archive-keyrings: downloading $_label from $_url"
	if command -v gpg >/dev/null 2>&1; then
		curl -fsSL "$_url" | gpg --dearmor >"$_dest"
	else
		echo "fetch-archive-keyrings: gpg required to convert $_url" >&2
		return 1
	fi
	chmod 0644 "$_dest"
	echo "fetch-archive-keyrings: installed $_label -> $_dest"
}

ensure_keyring() {
	_label=$1
	_dest=$2
	_host=$3
	shift 3

	[ -f "$_dest" ] && return 0
	copy_or_use "$_dest" "$_host" && return 0
	"$@"
}

fetch_debian() {
	ensure_keyring debian-archive-keyring \
		"$KEYRING_CACHE/debian-archive-keyring.gpg" \
		/usr/share/keyrings/debian-archive-keyring.gpg \
		_fetch_pool_deb_keyring debian-archive-keyring \
		"$KEYRING_CACHE/debian-archive-keyring.gpg" \
		"$SBUILD_DEBIAN_MIRROR" "$SBUILD_DIST" debian-archive-keyring \
		pool/main/d/debian-archive-keyring/debian-archive-keyring_2025.1_all.deb
}

fetch_raspbian() {
	_rasp_mirror=${SBUILD_RASPBIAN_MIRROR%/}
	case "$_rasp_mirror" in
		*/raspbian) ;;
		*) _rasp_mirror="${_rasp_mirror}/raspbian" ;;
	esac

	ensure_keyring raspbian-archive-keyring \
		"$KEYRING_CACHE/raspbian-archive-keyring.gpg" \
		/usr/share/keyrings/raspbian-archive-keyring.gpg \
		_fetch_pool_deb_keyring raspbian-archive-keyring \
		"$KEYRING_CACHE/raspbian-archive-keyring.gpg" \
		"$_rasp_mirror" "$SBUILD_DIST" raspbian-archive-keyring \
		pool/main/r/raspbian-archive-keyring/raspbian-archive-keyring_20120528.4_all.deb
}

fetch_raspberrypi() {
	_rpi_org=${SBUILD_RPI_MIRROR%/}
	case "$_rpi_org" in
		*archive.raspberrypi.org*) _key_url="${_rpi_org%/debian}/debian/raspberrypi.gpg.key" ;;
		*) _key_url="http://archive.raspberrypi.org/debian/raspberrypi.gpg.key" ;;
	esac

	ensure_keyring raspberrypi-archive-keyring \
		"$KEYRING_CACHE/raspberrypi-archive-keyring.gpg" \
		/usr/share/keyrings/raspberrypi-archive-keyring.gpg \
		_fetch_ascii_key raspberrypi-archive-keyring \
		"$KEYRING_CACHE/raspberrypi-archive-keyring.gpg" \
		"$_key_url"
}

case "$TARGET" in
	debian) fetch_debian ;;
	raspbian) fetch_raspbian ;;
	raspberrypi) fetch_raspberrypi ;;
	all)
		fetch_debian
		fetch_raspbian
		fetch_raspberrypi
		;;
	*)
		echo "fetch-archive-keyrings: unknown target: $TARGET" >&2
		exit 1
		;;
esac

chown -R "$(id -u):$(id -g)" "$KEYRING_CACHE" 2>/dev/null || true
chmod -R a+rX "$KEYRING_CACHE"
