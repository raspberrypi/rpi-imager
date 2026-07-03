#!/bin/sh
# Create the upstream orig tarball for 3.0 (quilt) source packages.
#
# Usage:
#   debian/mk-orig-tarball.sh [git-ref]
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
. "$TOP/debian/lib.sh"

REF="${1:-HEAD}"
OUTPUT="$OUTPUT_DIR/${PACKAGE}_${UPSTREAM}.orig.tar.xz"
PREFIX="${PACKAGE}-${UPSTREAM}/"

if ! git -C "$TOP" cat-file -e "${REF}^{commit}" 2>/dev/null; then
	echo "mk-orig-tarball: invalid git ref: $REF" >&2
	exit 1
fi

ensure_dirs

git -C "$TOP" archive --format=tar --prefix="$PREFIX" "$REF" \
	-- . ':(exclude)debian' | xz -c > "$OUTPUT"

echo "mk-orig-tarball: wrote $OUTPUT"
