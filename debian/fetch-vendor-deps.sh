#!/bin/sh
# Initialize vendored third-party git submodules for offline CMake builds.
#
# Submodule paths and pinned tags match src/dependencies/*.cmake:
#   xz         v5.8.3       (LIBLZMA_VERSION)
#   zstd       v1.5.7       (ZSTD_VERSION)
#   zlib       v1.3.2       (ZLIB_VERSION)
#   nghttp2    v1.69.0      (NGHTTP2_VERSION)
#   libarchive v3.8.7       (LIBARCHIVE_VERSION)
#   curl       curl-8_20_0  (CURL_VERSION 8.20.0)
#   libusb     v1.0.30      (LIBUSB_VERSION)
#
# Usage:
#   debian/fetch-vendor-deps.sh
set -eu

TOP=$(cd "$(dirname "$0")/.." && pwd)
cd "$TOP"

if ! git rev-parse --git-dir >/dev/null 2>&1; then
	echo "fetch-vendor-deps: not a git checkout" >&2
	exit 1
fi

if [ ! -f .gitmodules ]; then
	echo "fetch-vendor-deps: .gitmodules not found" >&2
	exit 1
fi

echo "fetch-vendor-deps: initializing submodules..."
git submodule sync --recursive
git submodule update --init --depth 1

check_submodule() {
	_path=$1
	_tag=$2
	_marker=$3

	if [ ! -f "$TOP/$_path/$_marker" ]; then
		echo "fetch-vendor-deps: missing $_path/$_marker" >&2
		return 1
	fi

	_actual=$(git -C "$TOP/$_path" describe --tags --exact-match 2>/dev/null || true)
	if [ "$_actual" != "$_tag" ]; then
		echo "fetch-vendor-deps: $_path at '$_actual', expected '$_tag'" >&2
		return 1
	fi
}

check_submodule src/dependencies/vendor/xz v5.8.3 CMakeLists.txt
check_submodule src/dependencies/vendor/zstd v1.5.7 build/cmake/CMakeLists.txt
check_submodule src/dependencies/vendor/zlib v1.3.2 CMakeLists.txt
check_submodule src/dependencies/vendor/nghttp2 v1.69.0 lib/CMakeLists.txt
check_submodule src/dependencies/vendor/libarchive v3.8.7 CMakeLists.txt
check_submodule src/dependencies/vendor/curl curl-8_20_0 CMakeLists.txt
check_submodule src/dependencies/vendor/libusb v1.0.30 libusb/libusb.h

echo "fetch-vendor-deps: ready"
