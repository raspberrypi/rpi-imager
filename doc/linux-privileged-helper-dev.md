# Linux privileged helper (developer guide)

Linux privileged helper path: built by default; use `RPI_IMAGER_USE_LINUX_HELPER=1` at runtime. Pass `-DRPI_IMAGER_DISABLE_LINUX_HELPER=ON` at configure time to omit the helper.

AppImage shipping builds add embedded GPG signature verification and single-binary helper elevation; see [linux-appimage-signing.md](linux-appimage-signing.md).

## Build

```bash
cmake -S src -B build
cmake --build build
```

Produces `rpi-imager` and `rpi-imager-writer` in the build directory by default.

Optional: omit the helper:

```bash
cmake -S src -B build -DRPI_IMAGER_DISABLE_LINUX_HELPER=ON
```

Release AppImage fingerprint pinning:

```bash
cmake -S src -B build \
  -DRPI_IMAGER_TRUSTED_APPIMAGE_KEY_FINGERPRINTS="YOURGPGFINGERPRINT"
```

## Runtime

**Polkit path** (unprivileged GUI):

```bash
export RPI_IMAGER_USE_LINUX_HELPER=1
./build/rpi-imager
```

**AppImage** (recommended for end users): set `RPI_IMAGER_USE_LINUX_HELPER=1` and run the signed AppImage. Privileged operations launch `pkexec $APPIMAGE --privileged-helper …` (no separate writer binary in the AppDir).

**Native / deb path**: first privileged operation launches `pkexec rpi-imager-writer` (or the writer beside the client binary). Install `debian/com.raspberrypi.rpi-imager.writer.policy` to `/usr/share/polkit-1/actions/` for production.

**Embedded path** (already root, kiosk installs):

```bash
sudo ./build/rpi-imager
```

Uses `LinuxEmbeddedBackend` in-process; no helper process.

## Diagnostics

```bash
export RPI_IMAGER_USE_LINUX_HELPER=1
./build/rpi-imager --test-privileged-helper /dev/sdX \
  --test-privileged-helper-allow-write --test-privileged-helper-bulk
```

Verify AppImage embedded signature (helper-enabled build):

```bash
./build/rpi-imager --verify-appimage-signature ./Raspberry_Pi_Imager-x86_64.AppImage
```

## Benchmark

```bash
export RPI_IMAGER_USE_LINUX_HELPER=1
./build/rpi-imager --benchmark-write /dev/sdX --benchmark-verify
```

Helper audit log: `$XDG_RUNTIME_DIR/rpi-imager/helper.log` or `/tmp/rpi-imager-helper.log`.
