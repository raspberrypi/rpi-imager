#!/bin/sh
# Create the upstream orig tarball for 3.0 (quilt) source packages.
#
# Usage:
#   debian/mk-orig-tarball.sh [git-ref]
#
# Writes ../rpi-imager_<version>.orig.tar.xz from git archive (debian/ excluded
# automatically because it is not part of the upstream tree export when using
# git archive with pathspec, but we export the full tree minus debian).
set -eu

TOP="$(cd "$(dirname "$0")/.." && pwd)"
cd "$TOP"

REF="${1:-HEAD}"
PACKAGE=$(dpkg-parsechangelog -SSource)
VERSION=$(dpkg-parsechangelog -SVersion)
UPSTREAM="${VERSION%%-*}"
OUTPUT="$(cd "$TOP/.." && pwd)/${PACKAGE}_${UPSTREAM}.orig.tar.xz"
PREFIX="${PACKAGE}-${UPSTREAM}/"

if git cat-file -e "${REF}^{commit}" 2>/dev/null; then
	:
else
	echo "mk-orig-tarball: invalid git ref: $REF" >&2
	exit 1
fi

# Export tracked project files except debian/ packaging metadata.
git archive --format=tar --prefix="$PREFIX" "$REF" \
	-- . ':(exclude)debian' | xz -c > "$OUTPUT"

echo "mk-orig-tarball: wrote $OUTPUT"
