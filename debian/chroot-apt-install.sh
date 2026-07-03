#!/bin/sh
# apt-get install tuned for rootless mmdebstrap chroots.
#
# Usage (inside chroot):
#   debian/chroot-apt-install.sh pkg [pkg...]
set -eu

export DEBIAN_FRONTEND=noninteractive

if [ -x /usr/sbin/debconf-set-selections ]; then
	echo 'man-db man-db/auto-update boolean false' | debconf-set-selections 2>/dev/null || true
fi

mkdir -p /var/log/apt

_APT_COMMON="
-o APT::Sandbox::User=root
-o APT::Log::TermlogEnable=false
-o Dpkg::Options::=--force-confdef
-o Dpkg::Options::=--force-confold
"

# shellcheck disable=SC2086
apt-get $_APT_COMMON update

# man-db postinst often fails in user-namespace chroots; verify requested packages only.
set +e
# shellcheck disable=SC2086
apt-get $_APT_COMMON install -y --allow-downgrades --no-install-recommends "$@"
_install_status=$?
set -e

dpkg --configure -a 2>/dev/null || true
# debhelper Requires man-db; configure in dependency order (rootless chroots).
for _pkg in man-db debhelper dh-exec fakeroot; do
	case " $* " in
		*" $_pkg "*) ;;
		*) continue ;;
	esac
	dpkg --configure "$_pkg" 2>/dev/null \
		|| dpkg --configure --force-depends "$_pkg" 2>/dev/null \
		|| true
done

pkg_is_usable() {
	_pkg=$1
	dpkg-query -W -f='${Status}' "$_pkg" 2>/dev/null \
		| grep -qE '^(install ok installed|install ok unpacked|install ok half-configured)$'
}

_failed=
for _pkg in "$@"; do
	if ! pkg_is_usable "$_pkg"; then
		_failed="$_failed $_pkg"
	fi
done

if [ -n "$_failed" ]; then
	echo "chroot-apt-install: required package(s) not installed:$_failed" >&2
	exit 1
fi

if [ "$_install_status" -ne 0 ]; then
	echo "chroot-apt-install: apt reported errors (ignored; required packages are installed)" >&2
fi
