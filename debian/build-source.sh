#!/bin/sh
# Build quilt source package (.orig.tar.xz, .debian.tar.xz, .dsc).
#
# Usage:
#   debian/build-source.sh [git-ref]
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"
. "$TOP/debian/lib.sh"

REF="${1:-HEAD}"
ensure_dirs

"$TOP/debian/mk-orig-tarball.sh" "$REF"

# Build source package in a temp area, then collect artifacts.
_builddir=$(mktemp -d)
trap 'rm -rf "$_builddir"' EXIT INT HUP TERM

_orig="$OUTPUT_DIR/${PACKAGE}_${UPSTREAM}.orig.tar.xz"
if [ ! -f "$_orig" ]; then
	echo "build-source: missing $_orig" >&2
	exit 1
fi

cp "$_orig" "$_builddir/"
tar -C "$_builddir" -xf "$_orig"
_src="${PACKAGE}-${UPSTREAM}"
cp -a "$TOP/debian" "$_builddir/$_src/"

(
	cd "$_builddir/$_src"
	dpkg-buildpackage -S -uc -us -d
)

for _f in \
	"${PACKAGE}_${VERSION}.debian.tar.xz" \
	"${PACKAGE}_${VERSION}.dsc" \
	"${PACKAGE}_${VERSION}_source.changes"
do
	if [ -f "$_builddir/$_f" ]; then
		mv "$_builddir/$_f" "$OUTPUT_DIR/"
		echo "build-source: wrote $OUTPUT_DIR/$_f"
	fi
done

echo "build-source: complete"
