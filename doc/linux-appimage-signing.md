# Linux AppImage signing and privileged-helper peer auth

Shipping AppImage builds use an embedded GPG signature (AppImage Type 2 `.sha256_sig` ELF section). The privileged helper verifies that signature on the client's `$APPIMAGE` path before accepting a wire connection. This mirrors the Windows Authenticode thumbprint pin and macOS Team ID checks.

No sidecar files are required: trust is carried inside the AppImage itself.

## Build-time configuration

`create-appimage.sh` enables the Linux helper and can pin trusted key fingerprints at configure time:

```bash
export RPI_IMAGER_TRUSTED_APPIMAGE_KEY_FINGERPRINTS="ABCD1234…"   # uppercase hex, semicolon-separated
export RPI_IMAGER_APPIMAGE_SIGN_KEY="security@raspberrypi.com"    # optional: sign after pack
./create-appimage.sh --qt-root=…
```

CMake cache variables (manual builds):

| Variable | Purpose |
|----------|---------|
| `RPI_IMAGER_DISABLE_LINUX_HELPER` | Omit helper code and `rpi-imager-writer` (default: helper **built**) |
| `RPI_IMAGER_TRUSTED_APPIMAGE_KEY_FINGERPRINTS` | GPG key fingerprints accepted by peer auth |

Fingerprints are compiled into `rpi_imager_identity.h` as `identity::kTrustedAppImageKeyFingerprints[]`.

When the fingerprint list is **empty** (typical local dev builds), the helper allows **unsigned** AppImages after logging a warning. Release builds should always pin at least one fingerprint.

## Signing an AppImage

After `linuxdeploy` produces the AppImage, `create-appimage.sh` can re-pack from the AppDir with `appimagetool` and embed a signature:

```bash
export RPI_IMAGER_APPIMAGE_SIGN_KEY="your-key-id-or-email"
export APPIMAGETOOL_SIGN_PASSPHRASE="…"   # CI / non-interactive signing
./create-appimage.sh
```

The script downloads `appimagetool-${ARCH}.AppImage` on demand and replaces the output file with the signed build.

To obtain a fingerprint for pinning:

```bash
gpg --with-colons --fingerprint "$RPI_IMAGER_APPIMAGE_SIGN_KEY" | awk -F: '/^fpr:/ {print toupper($10); exit}'
```

Re-run CMake/`create-appimage.sh` with `RPI_IMAGER_TRUSTED_APPIMAGE_KEY_FINGERPRINTS` set to that value before shipping.

## Runtime: AppImage-only helper launch

When `$APPIMAGE` is set (normal AppImage execution), the polkit client elevates the **same AppImage** instead of a separate `rpi-imager-writer` binary:

```
pkexec $APPIMAGE --privileged-helper --socket /tmp/rpi-imager-writer-….
```

The main binary handles `--privileged-helper` before Qt starts and runs the Qt-free writer service (`RpiImagerWriterServiceMainLinux`).

Debian/native installs without `$APPIMAGE` continue to use `pkexec rpi-imager-writer --socket …`.

## Polkit policy

AppImage self-elevation installs a per-path polkit action via `--install-elevation-policy` (or the first-run elevation flow). The policy pins `org.freedesktop.policykit.exec.path` to the AppImage file path.

Packaged `.deb` installs ship `debian/com.raspberrypi.rpi-imager.writer.policy` for `/usr/bin/rpi-imager-writer`.

## Peer auth summary

On each new client connection the helper:

1. Reads `APPIMAGE` from `/proc/<client-pid>/environ`.
2. If set: verifies the embedded GPG signature on that path and checks the signer fingerprint against the pinned list.
3. If the helper also runs from an AppImage, its `$APPIMAGE` must match the client's.
4. If `APPIMAGE` is unset and release keys are pinned: **reject** (native dev fallback is disabled).
5. If `APPIMAGE` is unset and no keys are pinned: allow co-installed `rpi-imager` / `rpi-imager-cli` beside the helper (dev only).

Enable the helper path at runtime:

```bash
export RPI_IMAGER_USE_LINUX_HELPER=1
./Raspberry_Pi_Imager-x86_64.AppImage
```

## Verification during development

With a helper-enabled build you can verify an AppImage path without starting the GUI:

```bash
./build/rpi-imager --verify-appimage-signature /path/to/Raspberry_Pi_Imager-x86_64.AppImage
```

Exit code `0` means the signature is valid and the key is trusted (or unsigned allowed when no keys are pinned).
