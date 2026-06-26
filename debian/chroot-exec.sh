#!/bin/sh
# Run a command inside an mmdebstrap rootfs (rootless via user namespaces).
#
# Usage:
#   debian/chroot-exec.sh <arch> <command> [args...]
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

ARCH="${1:?usage: chroot-exec.sh <arch> <command> [args...]}"
shift

if [ "$(chroot_backend_for "$ARCH")" != mmdebstrap ]; then
	echo "chroot-exec: mmdebstrap root missing for $ARCH" >&2
	exit 1
fi

ROOT=$(chroot_mmdebstrap_root "$ARCH")
NAME=$(chroot_name "$ARCH")
MODE=$(mmdebstrap_run_mode)

# Serialize command for chroot bash -lc.
_cmd=
for _arg in "$@"; do
	_cmd="$_cmd $(printf '%q' "$_arg")"
done

_hook=$(mktemp "${TMPDIR:-/tmp}/rpi-imager-chroot-exec.XXXXXX")
trap 'rm -f "$_hook"' EXIT INT HUP TERM

cat >"$_hook" <<EOF
#!/bin/sh
set -eu
root=\$1
for _bind in '$TOP' '$QT_CACHE' '$APPIMAGE_ROOT'; do
	[ -d "\$_bind" ] || continue
	mkdir -p "\$root\$_bind"
	if ! mountpoint -q "\$root\$_bind" 2>/dev/null; then
		mount --bind "\$_bind" "\$root\$_bind" || {
			echo "chroot-exec: bind mount failed for \$_bind" >&2
			exit 1
		}
	fi
done
chroot "\$root" env \\
	RPI_IMAGER_CHROOT=1 \\
	SCHROOT_CHROOT_NAME=$NAME \\
	QT_CACHE='$QT_CACHE' \\
	QT_VERSION='$QT_VERSION' \\
	APPIMAGE_ROOT='$APPIMAGE_ROOT' \\
	TOP='$TOP' \\
	bash -lc "$_cmd"
EOF
chmod +x "$_hook"

mmdebstrap --mode="$MODE" --variant=custom \
	--skip=check/empty,setup,update,download,extract,essential,configure,cleanup \
	--customize-hook="$_hook" \
	"$SBUILD_DIST" "$ROOT" "$SBUILD_DEBIAN_MIRROR"
