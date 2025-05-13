# Inno Setup Installer for Raspberry Pi Imager

This directory contains the necessary files to build a Windows installer using Inno Setup, which is a widely-used, free installer system for Windows applications.

## Requirements

- Inno Setup 6 or later (https://jrsoftware.org/isinfo.php)
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
6. Free for both commercial and non-commercial use

## Customizing

To modify the installer:

- Edit `rpi-imager.iss.in` to change settings, add files, or modify behavior
- The script uses CMake variables like `@CMAKE_BINARY_DIR@` which are replaced during configuration

## Testing

Test the installer in a clean Windows 10 environment to verify:

1. Installation works correctly
2. Application launches and functions properly
3. File associations work as expected
4. Uninstallation removes all components 