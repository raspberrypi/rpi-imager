## Contributing

### Linux

#### Get dependencies

- Install the build dependencies (Debian used as an example):

```sh
sudo apt install --no-install-recommends build-essential cmake git libgnutls28-dev
```

#### Get the source

```sh
git clone --depth 1 https://github.com/raspberrypi/rpi-imager
```

#### Build Qt

```sh
sudo ./qt/build-qt.sh
```

This will build and install the version of Qt preferred for Raspberry Pi Imager into /opt/Qt/<version>. You must use `sudo` for the installation step to complete.

#### Build the AppImage

```sh
./create-appimage.sh
./Raspberry_Pi_Imager-*.AppImage
```

### Windows

#### Get dependencies

- Get the Qt online installer from: https://www.qt.io/download-open-source
  - During installation, choose Qt 6.9 with Mingw64 64-bit toolchain.
- For building the installer, install Inno Setup scriptable install system: https://jrsoftware.org/isdl.php
- Install Visual Studio Code (or a derivative) and the Qt Extension Pack.
- It is assumed you already have a valid code signing certificate, and the Windows 10 Kit (SDK) installed.

#### Building

Building Raspberry Pi Imager on Windows is best done with Visual Studio Code (or a derivative).

- Open Visual Studio Code, and select 'Clone repo'. Give it the git url of this project.
- Open the CMake plugin settings, and set the following Configure Args:
  - `-DQt6_ROOT=C:\Qt\6.9.0\mingw_64` - or the equivalent path you installed Qt 6.9 to.
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
  - `-DQt6_ROOT=/opt/Qt/6.9.1/macos` - or the equivalent path you installed Qt 6.9 to.
  - `-DIMAGER_SIGNED_APP=ON` - to enable code signing.
  - `-DIMAGER_SIGNING_IDENTITY=$cn` - to specify the Developer ID Certificate Common Name.
  - `-DIMAGER_NOTARIZE_APP=ON` - to enable automatic notarization for distribution to others.
  - `-DIMAGER_NOTARIZE_KEYCHAIN_PROFILE=notarytool-password` - specify the name of the keychain item containing your Apple ID credentials for notarizing.
- In the CMake plugin tab, ensure you have selected the `MinSizeRel` variant if you intend to distribute to others.
- In the CMake plugin tab, select the 'rpi_imager' target, and build it
- Your resultant DMG will be located at `$WORKSPACE/build/Raspberry Pi Imager-$VERSION.dmg`

### Linux embedded (netboot) build

The Raspberry Pi Network installer (embedded imager) runs inside an operating system created by [pi-gen-micro](https://github.com/raspberrypi/pi-gen-micro/tree/main/configurations/rpi-imager-embedded).

To build the entire system, you must first build our customised embedded qt:

```sh
./qt/build-qt-embedded.sh
```

Then build the embedded AppImage:

```sh
./create-embedded.sh
```

Package the appImage for use with pi-gen-micro and other Debian systems:

```sh
dpkg-buildpackage -uc -us --profile=embedded
```

And finally, import your new embedded imager into pi-gen-micro for packaging:

```sh
rm ${pi-gen-micro-root}/packages/rpi-imager-embedded*.deb
cp ../rpi-imager-embedded*.deb ${pi-gen-micro-root}/packages/
pushd ${pi-gen-micro-root}/packages/ && dpkg-scanpackages . /dev/null | gzip -9c > Packages.gz && popd
```

### FreeBSD

To build manually, follow the steps below. An executable will be installed to `/usr/local/bin`. Both GUI builds and CLI-only builds will yield dynamically-linked executables (with the exception of zstd, which is statically linked to make use of experimental features).

#### Get dependencies

Install the dependencies through the FreeBSD binary database:

```sh
pkg install cmake git gnutls libarchive libidn2 nettle
```

`liblzma`, `zlib`, `zstd`, and are provided by the FreeBSD base system and should not need to be installed separately.

#### Get the source

```sh
git clone --depth 1 https://github.com/raspberrypi/rpi-imager
```

#### Build Qt
Unlike other systems, Qt does not build cleanly on FreeBSD without patches. Hence, you should opt to use a copy from the ports tree through one of three options:

1. Install from FreeBSD binary database (recommended):

   ```sh
   pkg install qt6-base
   ```
   
   If you plan to build the GUI version, some image plugins are required:
   ```sh
   pkg install qt6-imageformats
   ```

   If you need translations, an additional dependency is required:
   ```sh
   pkg install qt6-tools
   ```

2. Build FreeBSD port from source (with portmaster):

   First, install portmaster:
   ```sh
   pkg install portmaster
   ```

   Clone the ports tree if you have not yet done so:
   ```
   git clone https://git.freebsd.org/ports.git /usr/ports
   ```

   Then, install the dependencies:
   ```sh
   portmaster --delete-build-only /usr/ports/devel/qt6-base
   ```

   If you plan to build the GUI version, some image plugins are required:
   ```sh
   portmaster --delete-build-only /usr/ports/devel/qt6-imageformats
   ```

   If you need translations, an additional dependency is required:
   ```sh
   portmaster --delete-build-only /usr/ports/devel/qt6-tools
   ```

3. Build FreeBSD port from source (without portmaster):
   Clone the ports tree if you have not yet done so:
   ```
   git clone https://git.freebsd.org/ports.git /usr/ports
   ```

   Then, install the dependencies:
   ```sh
   cd /usr/ports/devel/qt6-base && make install clean
   ```

   If you plan to build the GUI version, some image plugins are required:
   ```sh
   cd /usr/ports/devel/qt6-imageformats && make install clean
   ```

   If you need translations, an additional dependency is required:
   ```sh
   cd /usr/ports/devel/qt6-tools && make install clean
   ```

#### Build and install the package

```sh
./create-freebsd.sh --cli --qt-root=/path/to/qt
```

which installs `rpi-imager-cli` for CLI-only version and

```sh
./create-freebsd.sh --qt-root=/path/to/qt
```

which installs `rpi-imager` for GUI version.

#### Uninstalling
1. CLI

   ``` sh
   rm -f /usr/local/bin/rpi-imager-cli
   rm -f /usr/local/share/icons/hicolor/scalable/apps/rpi-imager.svg
   rm -f /usr/local/share/applications/com.raspberrypi.rpi-imager.desktop
   ```

2. GUI

   ``` sh
   rm -f /usr/local/bin/rpi-imager
   rm -f /usr/local/share/icons/hicolor/scalable/apps/rpi-imager.svg
   rm -f /usr/local/share/applications/com.raspberrypi.rpi-imager.desktop
   rm -f /usr/local/share/metainfo/com.raspberrypi.rpi-imager.metainfo.xml
   rm -f /usr/local/share/mime/packages/com.raspberrypi.rpi-imager-manifest.xml
   rm -f /usr/local/share/polkit-1/actions/com.raspberrypi.rpi-imager.policy
   ```

