# Inno Setup Installer for Raspberry Pi Imager

This directory contains the necessary files to build a Windows installer using Inno Setup, which is a widely-used, free installer system for Windows applications.

## Requirements

- Inno Setup 6.5 or later 6.x; Inno Setup 7 is not yet supported (https://jrsoftware.org/isinfo.php)
- Visual Studio (2019 or later) with C++ workload
- CMake 3.16 or later

## Setup

1. Install Inno Setup from https://jrsoftware.org/isinfo.php
2. Ensure the Inno Setup Compiler (`iscc.exe`) is in your PATH or in the default installation directory

## Building the Installer

### Using CMake

The Inno Setup installer is integrated with the CMake build system. After configuring and building the project:

```bash
# Configure CMake with Inno Setup option enabled
cmake -DENABLE_INNO_INSTALLER=ON ..

# Build the application and installer
cmake --build . --target inno_installer
```

This will:
1. Create a `deploy` directory with all required files
2. Compile the Inno Setup script using `iscc.exe`
3. Place the installer EXE in the `installer` subdirectory of your build folder

## Windows 10 Minimum Requirement

This installer specifies Windows 10 as the minimum required OS version, which is set in both the Inno Setup script:
- Through the `MinVersion=10.0.15063` setting in the [Setup] section
- With explicit code that checks for Windows 10 during installation

## Why Inno Setup?

Inno Setup offers several advantages over NSIS:
1. Actively maintained with regular updates
2. Simpler script syntax that's easier to maintain
3. Modern installer look and feel
4. Better Unicode support
5. Cleaner uninstallation
6. Free to use, though commercial users are asked to buy a licence (https://jrsoftware.org/isorder.php)

## Customizing

To modify the installer:

- Edit `rpi-imager.iss.in` to change settings, add files, or modify behavior
- The script uses CMake variables like `@CMAKE_BINARY_DIR@` which are replaced during configuration

## Installer Languages

Most installer languages use the translations bundled with Inno Setup (`compiler:Languages\*.isl`).

Simplified Chinese is the exception. Inno Setup 6 does not install its Chinese translation, so `ChineseSimplified.isl` is kept in this directory, copied unchanged from the `is6` branch of https://github.com/jrsoftware/issrc (`Files/Languages/Unofficial/`). Refresh it from there when moving to a newer Inno Setup 6 release. Inno Setup 7.0.2 and later include it, so the copy can be removed once the build moves to Inno Setup 7.

Shortcut names come from the `ProgramName` custom message in `rpi-imager.iss.in`, so they follow the installer language. A localised name should match the app's own translation in `src/i18n/`. If you add one, also add matching `[InstallDelete]` entries, so that reinstalling in a different language removes the shortcut with the old name.

## Testing

Test the installer in a clean Windows 10 environment to verify:

1. Installation works correctly
2. Application launches and functions properly
3. File associations work as expected
4. Uninstallation removes all components 