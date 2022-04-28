# rpi-imager

Raspberry Pi Imaging Utility

- Download the latest version for Windows, macOS and Ubuntu from the [Raspberry Pi downloads page](https://www.raspberrypi.com/software/).
- To install on Raspberry Pi OS, use `sudo apt update && sudo apt install rpi-imager`.

## License

The main code of the Imaging Utility is made available under the terms of the Apache license.
See license.txt and files in "src/dependencies" folder for more information about the various open source licenses that apply to the third-party dependencies used such as Qt, libarchive, drivelist, mountutils and libcurl.
For the embedded (netboot) build see also "embedded/legal-info" for more information about the extra system software included in that.

## How to rebuild

### Debian/Ubuntu Linux

#### Get dependencies

Install the build dependencies:

```
sudo apt install --no-install-recommends build-essential devscripts debhelper cmake git libarchive-dev libcurl4-gnutls-dev liblzma-dev \
    qtbase5-dev qtbase5-dev-tools qtdeclarative5-dev libqt5svg5-dev qttools5-dev libgnutls28-dev \
    qml-module-qtquick2 qml-module-qtquick-controls2 qml-module-qtquick-layouts qml-module-qtquick-templates2 qml-module-qtquick-window2 qml-module-qtgraphicaleffects
```

#### Get the source

```
git clone --depth 1 https://github.com/raspberrypi/rpi-imager
```

#### Building on the Pi

If building on a device with limited memory (e.g. 1 GB Pi), disable parallel build or it may run out of memory:

```
export DEB_BUILD_OPTIONS="parallel=1"
```

#### Build the Debian package

```
cd rpi-imager
debuild -uc -us
```

debuild will compile everything, create a .deb package and put it in the parent directory.
Can install it with apt:

```
cd ..
sudo apt install ./rpi-imager*.deb
```

It should create an icon in the start menu under "Utilities" or "Accessories".
The imaging utility will normally be run as regular user, and will call udisks2 over DBus to perform privileged operations like opening the disk device for writing.
If udisks2 is not functional on your Linux distribution, you can alternatively start it as "root" with sudo and similar tools.

### Fedora/RHEL/CentOS Linux

#### Get dependencies

Install the build dependencies:

```
sudo yum install git gcc gcc-c++ make cmake libarchive-devel libcurl-devel lzma-sdk-devel openssl-devel qt5-qtbase-devel qt5-qtquickcontrols2-devel qt5-qtsvg-devel qt5-linguist xz-devel
```

#### Get the source

```
git clone --depth 1 https://github.com/raspberrypi/rpi-imager
```

#### Build and install the software

```
cd rpi-imager
mkdir -p build
cd build
cmake ../src
make
sudo make install
```

### Windows

#### Get dependencies

- Get the Qt online installer from: https://www.qt.io/download-open-source
During installation, choose a Qt 5.x with Mingw32 32-bit toolchain and CMake.

- If using the official Qt distribution that does NOT have schannel (Windows native SSL library) support, compile OpenSSL libraries ( https://wiki.qt.io/Compiling_OpenSSL_with_MinGW ) and copy the libssl/crypto DLLs to C:\qt\5.x\mingw73_32\bin  the include files to C:\qt\5.x\mingw73_32\include and the import library files to C:\qt\5.x\mingw73_32\lib

- For building installer get Nullsoft scriptable install system: https://nsis.sourceforge.io/Download

- It is assumed you already have a proper code signing certificate, and signtool.exe from the Windows SDK installed.
If NOT and are you only compiling for your own personal use, comment out all lines mentioning signtool from CMakelists.txt and the .nsi installer script.

#### Building

Building can be done manually using the command-line, using "cmake", "make", etc., but if you are not that familar with setting up a proper Windows build environment (setting paths, etc.), it is easiest to use the Qt creator GUI instead.

- Download source .zip from github and extract it to a folder on disk
- Open src/CMakeLists.txt in Qt creator.
- For builds you distribute to others, make sure you choose "Release" in the toolchain settings and not the debug flavour.
- Menu "Build" -> "Build all"
- Result will be in build_rpi-imager_someversion
- Go to the BUILD folder, right click on the .nsi script "Compile NSIS script", to create installer.

Note: the CMake integration in Qt Creator is a bit flaky at times. If you made any custom changes to the CMakeLists.txt file and it subsequently gets in an endless loop where it never finishes the "configures" stage while re-processing the file, delete "build_rpi-imager_someversion" directory and try again.

### macOS

#### Get dependencies

- Get the Qt online installer from: https://www.qt.io/download-open-source
During installation, choose a Qt 5.x edition and CMake.
- For creating a .DMG for distribution you can use an utility like: https://github.com/sindresorhus/create-dmg
- It is assumed you have an Apple developer subscription, and already have a "Developer ID" code signing certificate for distribution outside the Mac Store. (Privileged apps are not allowed in the Mac store)

#### Building

- Download source .zip from github and extract it to a folder on disk
- Start Qt Creator (may need to start "finder" navigate to home folder using the "Go" menu, and find Qt folder to start it manually as it may not have created icon in Applications), and open src/CMakeLists.txt
- Menu "Build" -> "Build all"
- Result will be in build_rpi-imager_someversion
- For distribution to others: code sign the .app, create a DMG, code sign the DMG, submit it for notarization to Apple and staple the notarization ticket to the DMG.

E.g.:

```
cd build-rpi-imager-Desktop_Qt_5_14_1_clang_64bit-Release/
codesign --deep --force --verify --verbose --sign "YOUR KEYID" --options runtime rpi-imager.app
mv rpi-imager.app "Raspberry Pi Imager.app"
create-dmg Raspberry\ Pi\ Imager.app
mv Raspberry\ Pi\ Imager\ .dmg imager.dmg
xcrun altool --notarize-app -t osx -f imager.dmg --primary-bundle-id="org.raspberrypi.imagingutility" -u YOUR-EMAIL-ADDRESS -p YOUR-APP-SPECIFIC-APPLE-PASSWORD -itc_provider TEAM-ID-IF-APPLICABLE
xcrun stapler staple imager.dmg
```

### Linux embedded (netboot) build

The embedded build runs under a minimalistic Linux distribution compiled by buildroot.
To build:

- You must be running a Linux system, and have the buildroot dependencies installed as listed in the buildroot manual: https://buildroot.org/downloads/manual/manual.html#requirement
- Run:

```
cd rpi-imager/embedded
./build.sh
```

The result will be in the "output" directory.
The files can be copied to a FAT32 formatted SD card, and inserted in a Pi for testing.
If you would like to build a (signed) netboot image there are tools for that at: https://github.com/raspberrypi/usbboot/tree/master/tools

## Other notes

### Debugging

On Linux and Mac the application will print debug messages to console by default if started from console.
On Windows start the application with the command-line option --debug to let it open a console window.

### Custom repository

If the application is started with "--repo [your own URL]" it will use a custom image repository.
So can simply create another 'start menu shortcut' to the application with that parameter to use the application with your own images.

### Telemetry

In order to understand usage of the application (e.g. uptake of Raspberry Pi Imager versions and which images and operating systems are most popular) when using the default image repository, the URL, operating system name and category (if present) of a selected image are sent along with the running version of Raspberry Pi Imager, your operating system, CPU architecture, locale and Raspberry Pi revision (if applicable) to https://rpi-imager-stats.raspberrypi.com by downloadstatstelemetry.cpp.

This web service is hosted by [Heroku](https://www.heroku.com) and only stores an incrementing counter using a [Redis Sorted Set](https://redis.io/topics/data-types#sorted-sets) for each URL, operating system name and category per day in the `eu-west-1` region and does not associate any personal data with those counts. This allows us to query the number of downloads over time and nothing else.

The last 1,500 requests to the service are logged for one week before expiring as this is the [minimum log retention period for Heroku](https://devcenter.heroku.com/articles/logging#log-history-limits).

On Windows, you can opt out of telemetry by disabling it in the Registry:

```
reg add "HKCU\Software\Raspberry Pi\Imager" /v telemetry /t REG_DWORD /d 0
```

On Linux, run `rpi-imager --disable-telemetry` or add the following to `~/.config/Raspberry Pi/Imager.conf`:

```ini
[General]
telemetry=false
```

On macOS, disable it by editing the property list for the application:

```
defaults write org.raspberrypi.Imager.plist telemetry -bool NO
```

### OS Customization

When using the app, press <kbd>CTRL</kbd> + <kbd>SHIFT</kbd> + <kbd>X</kbd> to reveal the **OS Customization** dialog.

In here, you can specify several things you would otherwise set in the boot configuration files. For example, you can enable SSH, set the Wi-Fi login, and specify your locale settings for the system image.
