# Linux release build

Every Linux artifact — the desktop and CLI AppImages, the `.deb` packages that
wrap them, and the embedded (netboot) package — is produced by one pipeline
driven from `debian/release.sh`. It builds each architecture inside its own
rootless `mmdebstrap` chroot, needs no `sudo`, and produces amd64, arm64 and
armhf output from a single machine of any of those three architectures.

This document is the reference for that pipeline. For the app itself, see
[CONTRIBUTING.md](../CONTRIBUTING.md); for the Qt builds it drives, see
[qt/README-qt-build.md](../qt/README-qt-build.md).

## The one rule

**There is no host-native build path.** Every architecture, the host's own
included, builds in a Debian **bookworm** chroot. This is deliberate, and
`debian/lib.sh` states why: a package built against the host's libraries is not
the package it claims to be. Build on a newer glibc than bookworm and the
binaries will not start on bookworm at all; build on an older one and features
silently vanish — `liburing` < 2.2 loses `io_uring` — while the artifact still
gets labelled for bookworm.

bookworm (glibc 2.36) is the baseline because it is new enough for the Qt version
we ship and `liburing` 2.2+, yet old enough that the resulting AppImages and
`.deb`s stay portable to current Debian, Ubuntu and Raspberry Pi OS releases.

## Prerequisites

On the build host:

```sh
sudo apt install mmdebstrap dpkg-dev git curl file xz-utils
```

(`dput` as well, if you set `DPUT_HOST`. Everything else the build needs is
installed *inside* the chroot from `debian/chroot-packages`.)

For any architecture that is not the host's, also:

```sh
sudo apt install qemu-user-static binfmt-support
```

`debian/mmdebstrap-ensure-chroot.sh` refuses to bootstrap a foreign-arch chroot
without both. `arch-test` is optional — when absent, mmdebstrap's `check/qemu`
step is skipped rather than failed.

Unprivileged user namespaces must be available (`MMDEBSTRAP_MODE=auto` picks
`unshare` whenever you are not root). Nothing in the pipeline calls `sudo` on
the host.

Clone with full history and no `--depth 1`: AppImage version strings come from
`git describe --tags --always --dirty`, and `debian/fetch-vendor-deps.sh`
initialises the vendored third-party submodules under `src/dependencies/vendor/`
and verifies each is at its pinned tag.

## Quick start

```sh
cp debian/release.conf.example debian/release.conf   # optional; edit to taste
debian/release.sh status                             # what exists, what doesn't
debian/release.sh repo                               # source + every arch
```

`status` is the first thing to run and the first thing to check when something
looks wrong. It prints the resolved version, every configured path, and the
state of each cache — chroots, Qt trees, staged AppImages — including whether a
staged AppImage's embedded runtime is the *right* architecture.

`repo` creates any missing chroots, builds the source package, then builds
AppImages and `.deb`s for each architecture in `RELEASE_ARCHES`, host
architecture first. If `DPUT_HOST` is set it uploads each `.changes` at the end.

## Commands

| Command | What it does |
| --- | --- |
| `release.sh status` | Version, paths, and the state of every cache. Read this first. |
| `release.sh source [git-ref]` | Quilt source package (`.orig.tar.xz`, `.debian.tar.xz`, `.dsc`) from `git-ref` (default `HEAD`). |
| `release.sh appimages <arch> [--use-cache]` | Build desktop + CLI AppImages, then sync them into the per-arch cache. `--use-cache` syncs already-staged files without building. |
| `release.sh binary <arch>` | Binary `.deb`s for `arch`, always inside that arch's chroot. |
| `release.sh embedded <arch>` | The embedded (linuxfb) `.deb`. arm64 only. |
| `release.sh arch <arch> [--use-cache]` | `appimages` then `binary`. |
| `release.sh repo` | `source`, then `arch` for every entry in `RELEASE_ARCHES`. |

`repo` does **not** build the embedded package: it is not in the default
`DEB_BUILD_PROFILES` (`desktop cli`) and has its own command. Run
`debian/release.sh embedded arm64` separately.

Lower-level scripts can be called directly and are useful when debugging one
stage: `mmdebstrap-ensure-chroot.sh`, `chroot-exec.sh`, `ensure-qt.sh`,
`build-appimages.sh`, `build-binary-chroot.sh`, `build-embedded.sh`,
`chroot-rm.sh`.

## Where things live

Everything the pipeline generates stays inside the working tree, under two
git-ignored directories:

```
.debian/
  chroots/bookworm-<arch>-rpi-imager/   build chroots (mmdebstrap rootfs)
  qt/<arch>/<version>/gcc_*/            per-arch Qt trees
  appimages/<arch>/                     staged AppImages, per architecture
  archive-keyrings/                     archive signing keys for mmdebstrap
out/debian/                             .dsc, .tar.xz, .deb, .changes, .buildinfo
```

All four paths are configurable — `CHROOT_ROOT`, `QT_CACHE`, `APPIMAGE_ROOT`,
`KEYRING_CACHE`, `OUTPUT_DIR` — via `debian/release.conf` or the environment.
Relative paths resolve against the repository root.

## How a build runs

### 1. Keyrings

`debian/fetch-archive-keyrings.sh` populates `.debian/archive-keyrings/` with
the Debian, Raspbian and Raspberry Pi archive keys. Host copies from
`/usr/share/keyrings` are reused when present; otherwise the keyring `.deb` is
downloaded from the archive and unpacked, or the ASCII key is dearmoured. The
Debian keyring is additionally checked for the trixie signing key and refreshed
if stale.

Keys are staged into `/tmp` before use, not read from the cache in place:
mmdebstrap's `unshare` mode runs apt and hooks as a subuid user that cannot
traverse a mode-700 `$HOME`.

### 2. Chroot

`debian/mmdebstrap-ensure-chroot.sh <arch>` bootstraps
`.debian/chroots/bookworm-<arch>-rpi-imager`. It writes a tarball in `unshare`
mode and then extracts it as your own user, so the tree is always removable
without `sudo`. A failed bootstrap cleans up after itself; a chroot is only
considered usable once `.rpi-imager-chroot-ok` exists.

Two mmdebstrap hooks do the configuration:

- **setup hook** (`mmdebstrap-setup-hook.sh`) installs the bootstrap keyring
  and `sources.list.d/bootstrap.sources` *inside* the target root, because apt
  runs with `Dir=$root` during bootstrap and `Signed-By` must point at a path
  under that root.
- **customize hook** (`mmdebstrap-configure-apt.sh` → `mirrors.sh`) replaces the
  bootstrap sources with the final per-arch repository cascade, then runs
  `chroot-apt-install.sh` over `debian/chroot-packages`.

The apt cascade, pinned via `debian/chroot-apt/preferences-*.pref`:

| Arch | Repositories, highest priority first |
| --- | --- |
| armhf | raspbian.raspberrypi.com (700) > archive.raspberrypi.com (600) > Debian (500) |
| arm64 | archive.raspberrypi.com (600) > Debian (500) |
| amd64 | Debian only |

Two details worth knowing. `apt` is named explicitly in `--include` because
`--variant=minbase` resolves to `?priority(required)` and Raspbian bookworm
ships apt as `Priority: important` — minbase alone leaves the armhf chroot with
no `apt-get`. And `chroot-apt-install.sh` tolerates apt exiting non-zero
(`man-db`'s postinst routinely fails in a user-namespace chroot) but then
verifies every requested package really is installed, so genuine failures still
stop the build.

### 3. Qt

`debian/ensure-qt.sh <arch>` fills the per-arch Qt cache, building on a miss
(`QT_BUILD=auto`). Three variants exist, all under
`.debian/qt/<arch>/<version>/`:

| Variant | Directory | Built by | Notes |
| --- | --- | --- | --- |
| desktop | `gcc_64`, `gcc_arm64`, `gcc_arm32` | `qt/build-qt.sh` | Full Qt Quick UI |
| CLI | `gcc_*_cli` | `qt/build-qt-cli.sh` | `-no-gui -no-widgets -no-opengl -no-xcb` … |
| embedded | `gcc_*_embedded` | `qt/build-qt-embedded.sh` | `-no-opengl -no-dbus -qpa linuxfb` |

The version itself is selected in exactly one place — `QT_VERSION_DEFAULT` in
`qt/qt-build-common.sh` — which `debian/lib.sh` reads to set `QT_VERSION`. Set
`QT_VERSION` in the environment or `release.conf` to override for a single run.

Cache hits are decided by running the cached `qmake -query QT_VERSION` and
comparing against `QT_VERSION` — which is also why foreign-arch Qt can only be
validated from inside that arch's chroot.

armhf desktop Qt is best-effort: `QT_DESKTOP_BUILD=try` (the default) attempts
the vendored build but accepts a system `qmake6` if it fails, and the armhf
desktop AppImage is allowed to fail without failing the run — the CLI AppImage
still gets built. Set `QT_DESKTOP_BUILD=required` to make it fatal.

A host-arch Qt already installed under `/opt/Qt/<version>` is symlinked into the
cache rather than rebuilt.

### 4. AppImages — build in the chroot, pack on the host

`debian/build-appimages.sh <arch>` splits each AppImage into two stages, and
this split is the part most worth understanding:

- **build** (`APPIMAGE_PACKAGING=build`, inside the target-arch chroot) compiles
  the app, installs it into `AppDir-<img-arch>/`, deploys Qt, prunes, and
  completes the library closure.
- **pack** (`APPIMAGE_PACKAGING=pack`, on the host) wraps that AppDir into an
  AppImage with `appimagetool`.

Packing runs on the host because `linuxdeploy` and `appimagetool` are themselves
AppImages and must match the machine executing them, not the target. Hence
`APPIMAGE_TOOL_ARCH=$HOST_ARCH` while `--arch` names the target.

The cross-pack trap: **`appimagetool` silently embeds its own (host) runtime
when `ARCH` is all you pass.** `appimage_pack_with_tool()` therefore always
passes an explicit `--runtime-file`, fetched from the AppImage `type2-runtime`
project and cached in `appimage-tools/runtime-<arch>`. If the runtime cannot be
obtained for a cross-pack, the build fails rather than producing a mislabelled
AppImage. `release.sh status` re-checks staged AppImages with `file(1)` and
flags any whose ELF header is the wrong architecture.

`linuxdeploy` has no armhf build, so armhf always goes through `appimagetool`.

`debian/sync-appimages.sh` then copies the versioned
`Raspberry_Pi_Imager-<git-describe>-{desktop,cli}-<arch>.AppImage` into
`.debian/appimages/<arch>/` and points the stable `rpi-imager-<arch>.AppImage`
symlinks at them.

### 5. Binary packages

`debian/build-binary-chroot.sh <arch>` stages the AppImages into the tree under
the names `debian/*.install` expects, then runs
`dpkg-buildpackage -b -a<arch> -P<profiles>` inside the chroot.

`debian/chroot-exec.sh` provides the chroot environment: it bind-mounts `$TOP`,
`$QT_CACHE` and `$APPIMAGE_ROOT`, rbind-mounts `/dev`, `/proc` and `/sys`, and
runs the command under `unshare --user --map-root-user` when not root, then
restores file ownership on the way out.

One consequence is easy to trip over: `dpkg-buildpackage` writes its output to
`$TOP/..`, and only `$TOP` is bind-mounted — so the artifacts land inside the
chroot's rootfs, not on the host's parent directory. `build-binary-chroot.sh`
moves them out of `<chroot>/<parent>/` into `out/debian/` afterwards.

`debian/rules` skips `dh_shlibdeps` and `dh_makeshlibs` for the desktop and CLI
packages: they ship self-contained AppImages, so there is nothing to scan, and
skipping it also avoids needing a cross-arch `objdump`. Their runtime
dependencies are therefore maintained by hand in `debian/control`.

### The embedded package

`debian/build-embedded.sh arm64` builds the dedicated `-no-opengl -no-dbus
-qpa linuxfb` Qt if missing, runs `create-embedded.sh` inside the arm64 chroot
to assemble the vendored `/opt` tree, and collects
`rpi-imager-embedded_<version>_arm64.deb` into `out/debian/`.

The dedicated Qt is not optional: the netboot target image (pi-gen-micro)
carries no Mesa/GL and no X11 — far too large for a network-loaded image — so
the embedded Qt must not link `libEGL`/`libGL`/`libX11` at all. arm64 is the
only architecture the embedded installer targets.

## What gets bundled, and what must not

Two exclusion predicates in `debian/lib.sh` decide this, and they are not the
same list.

`appimage_lib_excluded()` keeps glibc and the dynamic loader, the compiler
runtimes, the whole GPU stack, X11/Wayland/input, the font stack, and
`libsystemd`/`libdbus`/`libcap`/`libudev` **out** of the AppImage. Bundling any
of these couples the AppImage to the build host's C library, graphics stack or
session bus — bundled `libsystemd`/`libdbus` is what broke `lsblk` and udisks
integration in #1304 and #1577. Anything excluded here must be declared as a
`Depends` in `debian/control` instead.

`embedded_lib_excluded()` is much narrower, because the embedded tree is
deliberately self-contained under `/opt`: only the C library, compiler runtime,
GPU stack and `libsystemd` stay external.

`deploy_lib_closure_core()` then completes the transitive `DT_NEEDED` closure of
whatever survived. It reads ELF headers with `readelf` rather than shelling out
to `ldd`, so it works unchanged on a foreign-architecture tree, and it runs
*after* pruning so removed plugins do not drag their dependencies back in. An
unresolved soname that is neither excluded nor found is a hard error, with the
fix spelled out: add the providing package to `debian/chroot-packages`, or
exclude the soname and declare it in `debian/control`.

`prune_qml_to_imports()` trims the deployed QML tree to the modules the UI
actually imports, and is shared by the AppImage and embedded paths so the two
cannot drift. Prune **modules**, not libraries: dropping a library whose plugin
is still deployed leaves the plugin unable to load. Re-derive the import list
with:

```sh
grep -rhoE '^\s*import\s+[A-Za-z0-9_.]+' --include='*.qml' src/ | sort -u
```

## Configuration

`debian/release.conf` (git-ignored; copy from `debian/release.conf.example`) or
the environment. Environment wins.

| Variable | Default | Purpose |
| --- | --- | --- |
| `OUTPUT_DIR` | `out/debian` | Where `.dsc`/`.tar.xz`/`.deb` land |
| `APPIMAGE_ROOT` | `.debian/appimages` | Per-arch AppImage cache |
| `QT_CACHE` | `.debian/qt` | Per-arch Qt trees |
| `QT_VERSION` | `QT_VERSION_DEFAULT` from `qt/qt-build-common.sh` | Qt version to build and require |
| `QT_BUILD` | `auto` | `auto` build on miss, `cached` require, `always` rebuild |
| `QT_DESKTOP_BUILD` | `try` | armhf desktop Qt: `try` or `required` |
| `CHROOT_ROOT` | `.debian/chroots` | Chroot trees |
| `CHROOT_DIST` | `bookworm` | Chroot suite (see “The one rule”) |
| `CHROOT_SUFFIX` | `rpi-imager` | Chroot name suffix |
| `CHROOT_ARCHES` | `arm64 amd64 armhf` | Architectures chroots exist for |
| `CHROOT_AUTO_CREATE` | `auto` | `0` to require manual chroot setup |
| `MMDEBSTRAP_MODE` | `auto` | `unshare` when not root |
| `KEYRING_CACHE` | `.debian/archive-keyrings` | Archive keys for mmdebstrap |
| `DEBIAN_MIRROR` | `deb.debian.org/debian` | Debian mirror |
| `RASPBIAN_MIRROR` | `raspbian.raspberrypi.com/raspbian` | Raspbian mirror (armhf) |
| `RPI_MIRROR` | `archive.raspberrypi.com/debian` | Raspberry Pi archive |
| `DEB_BUILD_PROFILES` | `desktop cli` | Build profiles; `embedded` is separate |
| `APPIMAGE_BUILD` | `always` | `cached` to sync pre-staged AppImages only |
| `RELEASE_ARCHES` | host arch | Architectures `release.sh repo` covers |
| `DPUT_HOST` | unset | Upload target for `release.sh repo` |
| `APPIMAGE_REMOTE_<arch>` | unset | SSH builder fallback, `user@host[:/path]` |

`APPIMAGE_REMOTE_<arch>` is a fallback for architectures with no local chroot.
With rootless chroots working there is rarely a reason to use it.

## Troubleshooting

**`release.sh status` shows `!!` against a staged AppImage.** Its embedded
runtime is the wrong architecture. Confirm with `file .debian/appimages/<arch>/*.AppImage`
and rebuild; do not ship it. Check that `appimage-tools/runtime-<arch>` exists
and is non-empty.

**`mmdebstrap not installed`.** `sudo apt install mmdebstrap`.

**`install qemu-user-static on the host for <arch> builds`.** Install
`qemu-user-static` and `binfmt-support`; foreign-arch chroots cannot be
bootstrapped without them.

**Unresolved libraries at the end of a closure pass.** Read the list it prints.
Either the providing `-dev`/runtime package is missing from
`debian/chroot-packages`, or the soname genuinely belongs to the host and needs
adding to the exclusion predicate *and* to `debian/control`.

**`$TOP not visible inside <chroot>`.** The bind mounts did not take. Recreate
the chroot: `debian/chroot-rm.sh <arch> && debian/mmdebstrap-ensure-chroot.sh <arch>`.

**A chroot will not delete.** Use `debian/chroot-rm.sh` rather than `rm -rf`; it
escalates through mmdebstrap teardown, `setpriv` per owning uid,
`unshare --map-root-user` and `fakeroot` to deal with subuid-owned files, and
tells you what to ask an admin for if all of those fail.

**`ensure-qt: <arch> Qt must be built inside the <arch> chroot`.** You invoked
`ensure-qt.sh` directly for a foreign architecture. Go through
`debian/build-appimages.sh <arch>`, which enters the chroot first.

## Cleaning up

```sh
debian/chroot-rm.sh <arch>     # one chroot
debian/chroot-rm.sh --all      # every chroot and the keyring cache
rm -rf out .debian/appimages   # artifacts and staged AppImages
```

The Qt cache under `.debian/qt/` is the expensive one; keep it unless
`QT_VERSION` changes.
