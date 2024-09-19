# Raspberry Pi Imager

Raspberry Pi Imaging Utility

- To install on Raspberry Pi OS, use `sudo apt update && sudo apt install rpi-imager`.
- Download the latest version for Windows, macOS and Ubuntu from the [Raspberry Pi downloads page](https://www.raspberrypi.com/software/).

## How to use Raspberry Pi Imager

Please see our [official documentation](https://www.raspberrypi.com/documentation/computers/getting-started.html#raspberry-pi-imager).

## Contributing

### Linux

#### Get dependencies

- Install the build dependencies (Debian used as an example):

```sh
sudo apt install --no-install-recommends build-essential cmake git libgnutls28-dev
```

- Get the Qt online installer from: https://www.qt.io/download-open-source
- During installation, choose Qt 6.7, CMake and Qt Creator.

#### Get the source

```sh
git clone --depth 1 https://github.com/raspberrypi/rpi-imager
```

#### Build the AppImage

Modify appimagecraft.yml:

- First, you _must_ set Qt6_ROOT (as a extra_variables item under build/cmake) to the root of your Qt6 installation. eg: `/opt/Qt/6.7.2/gcc_arm64/`
- Second, you _must_ set QMAKE (as a raw_environment variable of the linuxdeploy plugin) to the full path of qmake inside that Qt6 installation. eg: `/opt/Qt/6.7.2./gcc_arm64/bin/qmake`

Now, use AppImageCraft to build your AppImage:

```sh
cd rpi-imager
export LD_LIBRARY_PATH=${your_Qt6_install_path}/lib
./${your_platform_appimagecraft}.AppImage
```

Now mark the AppImage as executable, and run it:

```sh
chmod +x ./Raspberry_Pi_Imager-*.AppImage
./Raspberry_Pi_Imager-*.AppImage
```

### Windows

#### Get dependencies

- Get the Qt online installer from: https://www.qt.io/download-open-source
During installation, choose Qt 6.7 with Mingw64 64-bit toolchain, CMake and Qt Creator.

- For building the installer, get Nullsoft scriptable install system: https://nsis.sourceforge.io/Download

- It is assumed you already have a valid code signing certificate, and the Windows 10 Kit (SDK) installed.

#### Building

Building Raspberry Pi Imager on Windows is best done with the Qt Creator GUI.

- Download source .zip from github and extract it to a folder on disk
- Open src/CMakeLists.txt in Qt Creator.
- Use Qt Creator to set the MINGW64_ROOT CMake variable to your MingGW64 installation path, eg `C:\Qt\Tools\mingw64`
- For builds you distribute to others, make sure you choose "Release" in the toolchain settings and not the Debug configuration.
- Menu "Build" -> "Build all"
- Result will be in build_rpi-imager_someversion
- Go to the BUILD folder, right click on the .nsi script "Compile NSIS script", to create installer.

### macOS

#### Get dependencies

- Get the Qt online installer from: https://www.qt.io/download-open-source
During installation, choose Qt 6.7, CMake and Qt Creator.
- It is assumed you have an Apple developer subscription, and already have a "Developer ID" code signing certificate for distribution outside the Mac Store.

#### Building

- Download source .zip from github and extract it to a folder on disk
- Start Qt Creator and open src/CMakeLists.txt
- Use Qt Creator to set the Qt6_ROOT CMake variable to your Qt6 installation path, eg `/opt/Qt6/6.7.2/gcc_arm64`
- Menu "Build" -> "Build all"
- Result will be in build_rpi-imager_someversion
- For distribution to others:
  - Use the IMAGER_SIGNED_APP flag to enable Application signing
  - Use the IMAGER_SIGNING_IDENTITY string to specify the Developer ID certificate Common Name
  - Use the IMAGER_NOTARIZE_APP flag to enable notarization as part of the build
  - Use the IMAGER_NOTARIZE_KEYCHAIN_PROFILE string to specify the name of the keychain item containing your Apple ID credentials for notarizing.

### Linux embedded (netboot) build

The embedded build runs under a minimalistic Linux distribution compiled by buildroot.
To build:

- You must be running a Linux system, and have the buildroot dependencies installed as listed in the buildroot manual: https://buildroot.org/downloads/manual/manual.html#requirement
- Run:

```sh
cd rpi-imager/embedded
./build.sh
```

The result will be in the "output" directory.
The files can be copied to a FAT32 formatted SD card, and inserted in a Pi for testing.
If you would like to build a (signed) netboot image there are tools for that at: https://github.com/raspberrypi/usbboot/tree/master/tools

## Other notes

### Custom repository

If the application is started with "--repo [your own URL]" it will use a custom image repository.
So can simply create another 'start menu shortcut' to the application with that parameter to use the application with your own images.

### Telemetry

In order to understand usage of the application (e.g. uptake of Raspberry Pi Imager versions and which images and operating systems are most popular) when using the default image repository, the URL, operating system name and category (if present) of a selected image are sent along with the running version of Raspberry Pi Imager, your operating system, CPU architecture, locale and Raspberry Pi revision (if applicable) to https://rpi-imager-stats.raspberrypi.com by downloadstatstelemetry.cpp.

This web service is hosted by [Heroku](https://www.heroku.com) and only stores an incrementing counter using a [Redis Sorted Set](https://redis.io/topics/data-types#sorted-sets) for each URL, operating system name and category per day in the `eu-west-1` region and does not associate any personal data with those counts. This allows us to query the number of downloads over time and nothing else.

The last 1,500 requests to the service are logged for one week before expiring as this is the [minimum log retention period for Heroku](https://devcenter.heroku.com/articles/logging#log-history-limits).

On Windows, you can opt out of telemetry by disabling it in the Registry:

```pwsh
reg add "HKCU\Software\Raspberry Pi\Imager" /v telemetry /t REG_DWORD /d 0
```

On Linux, run `rpi-imager --disable-telemetry` or add the following to `~/.config/Raspberry Pi/Imager.conf`:

```ini
[General]
telemetry=false
```

On macOS, disable it by editing the property list for the application:

```sh
defaults write org.raspberrypi.Imager.plist telemetry -bool NO
```

### License

The main code of the Imaging Utility is made available under the terms of the Apache license.
See license.txt and files in "src/dependencies" folder for more information about the various open source licenses that apply to the third-party dependencies used such as Qt, libarchive, drivelist, mountutils and libcurl.
For the embedded (netboot) build see also "embedded/legal-info" for more information about the extra system software included in that.
