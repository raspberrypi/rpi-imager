## Contributing

### Linux

Linux artifacts are built by one pipeline, driven from `debian/release.sh`. It
builds every architecture — amd64, arm64 and armhf — inside its own rootless
`mmdebstrap` chroot, from a machine of any of those architectures, and needs no
`sudo`. [doc/linux-build.md](./doc/linux-build.md) is the full reference; this is
the short version.

#### Get dependencies

Only what is needed to drive the pipeline; the actual build dependencies are
installed inside the chroot:

```sh
sudo apt install mmdebstrap dpkg-dev git curl file xz-utils
```

To build an architecture other than your own, also install `qemu-user-static`
and `binfmt-support`.

#### Get the source

```sh
git clone https://github.com/raspberrypi/rpi-imager
```

Clone with full history: version strings come from `git describe --tags`, and
the vendored third-party dependencies are git submodules (initialised for you by
`debian/fetch-vendor-deps.sh`). A `--depth 1` clone will not build.

#### Build the release artifacts

```sh
debian/release.sh status          # what exists, what doesn't — check this first
debian/release.sh appimages amd64 # desktop + CLI AppImages for one architecture
debian/release.sh arch amd64      # ...and the .deb packages that wrap them

# All three architectures, plus the source package. RELEASE_ARCHES defaults to
# your own architecture alone, so pass it explicitly (or set it in release.conf).
RELEASE_ARCHES="amd64 arm64 armhf" debian/release.sh repo
```

`repo` is the only command that builds more than one architecture; the others
take exactly one.

The first run bootstraps a chroot and builds Qt, so it takes a while; both are
cached under `.debian/` afterwards. Finished AppImages land in
`.debian/appimages/<arch>/` and packages in `out/debian/`.

#### Build quickly while developing

To iterate on the app itself, skip the packaging and build against a Qt tree
directly:

```sh
debian/ensure-qt.sh amd64    # populates .debian/qt/, or use system qt6-base-dev
cmake -B build -G Ninja src -DQt6_ROOT=$PWD/.debian/qt/amd64/<version>/gcc_64
cmake --build build
```

`<version>` is whatever `QT_VERSION_DEFAULT` in
[qt/qt-build-common.sh](./qt/qt-build-common.sh) says — the single place the Qt
version is selected.

### Windows

#### Get dependencies

- Get the Qt online installer from: https://www.qt.io/download-open-source
  - During installation, choose the Qt version named by `QT_VERSION_DEFAULT` in [qt/qt-build-common.sh](./qt/qt-build-common.sh), with the Mingw64 64-bit toolchain. Any newer Qt 6 that satisfies the `find_package(Qt6 ...)` minimum in `src/CMakeLists.txt` will also configure.
- For building the installer, install Inno Setup scriptable install system: https://jrsoftware.org/isdl.php
- Install Visual Studio Code (or a derivative) and the Qt Extension Pack.
- It is assumed you already have a valid code signing certificate, and the Windows 10 Kit (SDK) installed.

#### Building

Building Raspberry Pi Imager on Windows is best done with Visual Studio Code (or a derivative).

- Open Visual Studio Code, and select 'Clone repo'. Give it the git url of this project.
- Open the CMake plugin settings, and set the following Configure Args:
  - `-DQt6_ROOT=C:\Qt\<version>\mingw_64` - or the equivalent path you installed Qt to.
  - `-DMINGW64_ROOT=C:\Qt\Tools\mingw1310_64` - or the equivalent path you installed mingw64 to.
  - `-DENABLE_INNO_INSTALLER=ON` - to enable the Inno Setup installer, rather than the legacy NSIS installer.
  - `-DIMAGER_SIGNED_APP=ON` - to enable code signing for redistribution.
- In the CMake plugin tab, ensure you have selected the `MinSizeRel` variant if you intend to distribute to others.
- In the CMake plugin tab, select the 'inno_installer' target, and build it
- Your resultant installer will be located in `%WORKSPACE%\build\installer`

### macOS

#### Get dependencies

- Build a minimal Qt from source using our build script:
  ```bash
  ./qt/build-qt-macos.sh
  ```
  - This builds only what's needed for rpi-imager, resulting in faster builds and smaller size
  - See `qt/README-qt-build-macos.md` for detailed instructions
- Install Visual Studio Code (or a derivative), and the Qt Extension Pack.
- It is assumed you have an Apple developer subscription, and already have a "Developer ID" code signing certificate for distribution outside the Mac Store.

#### Building

Building Raspberry Pi Imager on macOS is best done with Visual Studio Code (or a derivative).

- Open Visual Studio Code, and select 'Clone repo'. Give it the git url of this project.
- Open the CMake plugin settings, and set the following Configure Args:
  - `-DQt6_ROOT=/opt/Qt/<version>/macos` - or the equivalent path `build-qt-macos.sh` installed Qt to.
  - `-DIMAGER_SIGNED_APP=ON` - to enable code signing.
  - `-DIMAGER_SIGNING_IDENTITY=$cn` - to specify the Developer ID Certificate Common Name.
  - `-DIMAGER_NOTARIZE_APP=ON` - to enable automatic notarization for distribution to others.
  - `-DIMAGER_NOTARIZE_KEYCHAIN_PROFILE=notarytool-password` - specify the name of the keychain item containing your Apple ID credentials for notarizing.
- In the CMake plugin tab, ensure you have selected the `MinSizeRel` variant if you intend to distribute to others.
- In the CMake plugin tab, select the 'rpi_imager' target, and build it
- Your resultant DMG will be located at `$WORKSPACE/build/Raspberry Pi Imager-$VERSION.dmg`

### Linux embedded (netboot) build

The Raspberry Pi Network installer (embedded imager) runs inside an operating system created by [pi-gen-micro](https://github.com/raspberrypi/pi-gen-micro/tree/main/configurations/rpi-imager-embedded).

It uses a **dedicated** Qt, distinct from the desktop and CLI release Qt: built
`-no-opengl -no-dbus -qpa linuxfb` by `qt/build-qt-embedded.sh` into its own
cache variant (`gcc_arm64_embedded`). The netboot target image carries no
Mesa/GL, no X11 and no session bus — far too large for a network-loaded image —
so the embedded Qt must not link `libEGL`/`libGL`/`libX11` at all. The build
below produces it automatically on a cache miss.

The canonical build goes through the release pipeline, which builds inside the
arm64 mmdebstrap chroot:

```sh
debian/release.sh embedded arm64
```

This produces `out/debian/rpi-imager-embedded_<version>_arm64.deb`. It stages the
vendored `/opt` tree with `create-embedded.sh`, then assembles the `.deb` with
debhelper so that `debian/control` is the single source of the package's
dependencies and metadata (`dh_shlibdeps` is deliberately not used — the package
vendors its libraries, so the external `Depends` are maintained explicitly in
the `rpi-imager-embedded` stanza of `debian/control`).

To build against a Qt tree you resolved yourself, `create-embedded.sh` can be
run directly:

```sh
./create-embedded.sh --arch=aarch64 --qt-root=/path/to/qt
```

Finally, import the package into pi-gen-micro:

```sh
rm ${pi-gen-micro-root}/packages/rpi-imager-embedded*.deb
cp out/debian/rpi-imager-embedded*.deb ${pi-gen-micro-root}/packages/
pushd ${pi-gen-micro-root}/packages/ && dpkg-scanpackages . /dev/null | gzip -9c > Packages.gz && popd
```
