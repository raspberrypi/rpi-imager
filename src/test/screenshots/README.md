# Desktop UI screenshots

The screenshots the AppStream metainfo points at are ancient. Every one of them
carries "v1.2" in the title bar, and some still call the OS "Raspbian"; they are
served from `downloads.raspberrypi.org/imager/` and nothing in the tree
regenerates them, so they have simply aged in place since 2020
([#780](https://github.com/raspberrypi/rpi-imager/issues/780)).

[`shots.sh`](shots.sh) regenerates them from the current build, and eight more
pages besides.

```sh
cmake -S src -B build -DBUILD_TESTING=ON -DENABLE_TEST_HOOKS=ON
cmake --build build --target screenshots_desktop
./shots.sh /path/to/rpi-imager ./shots     # or directly
```

It needs `-DENABLE_TEST_HOOKS=ON`, which compiles in the `RPI_IMAGER_SCREENSHOT`
hook it drives. Release builds leave that hook out deliberately — it writes a
capture of the window, which on the customisation steps holds a Wi-Fi key and a
user password, to a caller-chosen path — so configuring without the flag gives
no `screenshots_desktop` target, and pointing `shots.sh` at such a build says
the hook is missing rather than timing out.

Output is one PNG per page, named for the page, plus a `metainfo/` directory
holding the five under the names the metainfo publishes them as.

## What is real and what is substituted

The frames are of the real application: its QML, its layout, its OS list, and
for the write pages its write path. Three things are substituted, and only so
that a run on one machine matches a run on another:

| | |
|-|-|
| the drive list | a stand-in `lsblk` on `PATH`. [`drivelist_linux.cpp`](../../drivelist/drivelist_linux.cpp) starts `lsblk` through `PATH` and reads nothing but its JSON, so this is the whole of the substitution — and it keeps the build host's real disks off the storage page |
| the display | Qt's offscreen platform, on a screen large enough that nothing is clipped, so no X or Wayland session is needed |
| privilege | an unprivileged user namespace maps the run to uid 0, which is all the imager's elevation check reads. Without it every page renders under a modal permissions dialog |

Nothing about the *pages* is mocked. In particular the writing and completion
pages are not staged widgets: they are photographs of a real write, of a real
image file, through `setSrc()`/`setDst()`/`startWrite()` — the same calls a
click drives — with a sparse file standing in for the card. No block device is
touched, and the destination costs only what the write puts in it.

## Why the writing page is caught at a percentage

A fixed delay photographs a different point on every machine. The 512 MB
fixture takes seconds on a real card and a fraction of one on tmpfs, so a delay
tuned to a laptop returns the *finished* page on a build host. `RPI_SHOT_WRITE_AT`
instead names how far through the write the frame is taken, and the hook waits
on the same progress signal the page draws from. The completion page is not
timed at all: the wizard advances to it when the write succeeds, exactly as in a
real run, and the hook waits for it to arrive.

## Variables

| Variable | Effect |
|----------|--------|
| `RPI_SHOT_FILTER` | glob; render only matching page names |
| `RPI_SHOT_PAGES` | alternative page list |
| `RPI_SHOT_SCALE` | `QT_SCALE_FACTOR`; the window is 680×450, so the default 2 publishes at 1360×900 |
| `RPI_SHOT_REPO` | OS list URL or file |
| `RPI_SHOT_WRITE_AT` | how far through the write to photograph it (default 45%) |
| `RPI_SHOT_IMAGE_SIZE` | source image to write, in numfmt(1) IEC units (default 512M) |
| `RPI_SHOT_CACHE` | where the OS list and source image are kept between runs |
| `RPI_SHOT_DELAY_MS` | settle time before the page is opened (default 6000) |
| `RPI_SHOT_TIMEOUT` | per-page timeout in seconds (default 300) |
| `RPI_SHOT_KEEP` | keep the fixtures and app logs |

The runner exits 4 where the host can offer neither root nor an unprivileged
user namespace, rather than quietly producing thirteen pictures of a
permissions dialog.

## Reproducibility

Two runs weeks apart differ only where the app does. The OS list is the one
moving part — its contents change under us — so the first run downloads it into
the cache and every later run reuses that copy; point `RPI_SHOT_REPO` at a
pinned file to be certain. The private `HOME` holds the app's `QSettings`,
including a pinned `textScaleFactor`, so the layout is the scale factor's doing
alone and no saved window position or language leaks in from the caller's real
profile.

## Adding a page

Append a row to [`pages.txt`](pages.txt): the `WizardContainer` step constant
without its `step` prefix, `jump` or `write`, and the name the page is published
under or `-`. `write` is only for the two pages that cannot exist without a
write behind them; everything else is reachable by opening it.

## What this does not cover

Offscreen rendering is not the compositor the desktop actually runs under, so
these frames confirm layout, text metrics and content rather than anything
about window decoration, shadows or the platform theme. They are also Linux
frames: the macOS and Windows listings need their own captures, because the
title bar and the native file dialogs are the platform's, not ours.
