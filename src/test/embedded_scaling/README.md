# Embedded display scaling matrix

`PlatformQuirks::applyEmbeddedDisplayScaling()` picks the UI scale for the
embedded (linuxfb) imager, which runs with no window manager or compositor to
negotiate DPI. It reads the connected display straight out of `/sys/class/drm`
— `status`, `modes` and `edid` — and sets `QT_SCALE_FACTOR` before Qt starts.

That makes it untestable in the ordinary way: on a build host the code sees
that host's display, or on a headless builder no display at all. So this
harness gives the real, unmodified function a display to look at. For each
case it builds a synthetic `/sys/class/drm`, bind-mounts it over the real one
inside an unprivileged mount namespace, and checks which factor the code then
chooses.

Nothing here is a mock: the binary under test is the production code path, and
the fixtures are checksum-correct EDID blocks of the shape real panels emit.

There are two runners over the same mechanism:

| | |
|-|-|
| [`run.sh`](run.sh) | checks *which* factor the code picks, for every case in [`cases.txt`](cases.txt) |
| [`screenshots.sh`](screenshots.sh) | renders the UI at that factor and writes a PNG per profile in [`profiles.txt`](profiles.txt) |

## Checking the chosen factor

```sh
ctest -R embedded_scaling_matrix          # from a BUILD_TESTING=ON build tree
cmake --build . --target test_embedded_scaling
./run.sh /path/to/embedded_scaling_probe  # directly
```

| Variable | Effect |
|----------|--------|
| `RPI_SCALING_FILTER` | glob; run only matching case names |
| `RPI_SCALING_CASES` | alternative case list |
| `RPI_SCALING_KEEP` | keep the fixture trees and probe logs |

The runner exits 4 — which CTest reads as a skip — where the host cannot
provide an unprivileged mount namespace, so a container that forbids
`unshare -rm` reports a skip rather than a failure.

## Rendering the UI

`run.sh` proves the right *number* was chosen. To see whether the interface
actually lays out at that number, `screenshots.sh` renders it:

```sh
cmake -S src -B build -DBUILD_EMBEDDED=ON -DENABLE_TEST_HOOKS=ON
cmake --build build --target screenshots_embedded_scaling
./screenshots.sh /path/to/rpi-imager ./shots
```

It needs a GUI build configured with `-DBUILD_EMBEDDED=ON`, so the embedded code
paths and layout are the ones exercised, and `-DENABLE_TEST_HOOKS=ON`, which
compiles in the screenshot hook it drives. Release builds leave that hook out
deliberately — it writes a capture of the window to a caller-chosen path, in a
binary the embedded image runs as root — so configuring without the flag gives
no `screenshots_embedded_scaling` target, and pointing `screenshots.sh` at such
a build reports that the hook is missing rather than timing out. Per profile it
builds the same
kind of fake `/sys/class/drm`, lets the app's own scaling code read it, renders
onto an offscreen screen of that panel's resolution, and saves the frame using
the app's `RPI_IMAGER_SCREENSHOT` hook. A profile fails if the factor chosen is
not the one the profile expects, or if the frame does not come back at the
panel's full resolution — a UI that did not fill the screen, or a device pixel
ratio that did not take.

| Variable | Effect |
|----------|--------|
| `RPI_SCALING_STEP` | jump the wizard to this step first, naming the output `<profile>.<step>.png` — either an index or a `WizardContainer` constant without its prefix, e.g. `WifiCustomization` |
| `RPI_SCALING_FORCE_SCALE` | render at this factor instead of the chosen one, naming the output `<profile>@<n>x.png` |
| `RPI_SCALING_REPO` | OS list URL or file, for a deterministic first screen |
| `RPI_SCALING_DELAY_MS` | settle time before the grab (default 3000) |
| `RPI_SCALING_TIMEOUT` | per-profile timeout in seconds (default 90) |

Rendering offscreen is not the linuxfb path the device uses, so these frames
confirm layout, scaling and text metrics rather than anything about the
framebuffer itself.

Panels shorter than 540 logical rows — the 7-inch DSI screen among them — clip
the bottom of the sidebar on customisation steps, and no scale factor can fix
that: 480 rows cannot hold 540 rows of sidebar. It needs a layout change, and it
is what those panels do today.

## Adding a case

Append a row to [`cases.txt`](cases.txt): a name, the `QT_SCALE_FACTOR` the run
must produce (or `unset`), extra environment (or `-`), and the connectors to
synthesise. Expected values come from the documented policy, not from a
recorded run — work out what the panel *should* get, then let the case prove
the code agrees. The policy fits the 680×540 canvas the embedded layout is drawn
for to the panel and ignores physical size entirely: `min(width/680,
height/540)`, snapped down to the nearest factor that divides the panel exactly
in both axes, held at 1.0 below a fit of 1.125, and clamped to `[1.0, 6.0]`.
The height is not the desktop window's 450: the customisation steps expand the
sidebar to eleven entries, and 480 rows clips "Done" off the bottom where 540
fits it.

EDID recipes are passed to [`mkedid.sh`](mkedid.sh) as a comma-separated list:

| Field | Meaning |
|-------|---------|
| `px=WxH` | preferred timing resolution |
| `mm=WxH` | image size in the detailed timing descriptor, the source the parser prefers |
| `cm=WxH` | coarse size in bytes 0x15/0x16; defaults to `mm` ÷ 10, and cannot exceed 255 |
| `no-dtd-size` | zero the descriptor's mm fields, leaving only the centimetre bytes |
| `monitor-descriptor` | make descriptor 1 a monitor descriptor, so it carries no size at all |
| `extensions=N` | append CTA-861 blocks, as every real 4K television does |
| `bad-header` | corrupt the fixed EDID header |
| `truncate=N` | emit a short, unusable block |

Blocks are valid EDID, so a case can also be replayed against a real kernel by
handing the same blob to `drm.edid_firmware=` in a VM.

## What the matrix pins down

The panel class that drove this harness is a **43-inch-class 4K TV used as a
desktop monitor**, and what it exposed was not a mis-tuned threshold but the
wrong input. The embedded UI is a fixed composition — a 200 px sidebar, a
content column capped at `Style.sectionMaxWidth`, 40 px buttons — so handed a
2560×1440 logical canvas it does not grow into it; it draws the same small
composition in the middle of a large screen. Density was never the problem: at
the old 1.5× the `NEXT` button measured 7.7 mm on a 32-inch 4K panel against
5.4 mm on the 7-inch DSI screen, so it was already physically *larger*.

So the factor now fits the design canvas to the panel and ignores physical size.
Two properties are worth keeping an eye on, and the matrix pins both:

- **Every 4K panel gets the same factor** (4, a 960×540 canvas), whether the
  glass is 24 inches or 98. Apparent size then tracks viewing distance, which
  tracks panel size. Renders at 4K, 1440p and 1080p are pixel-identical once
  normalised for their own factor.
- **The factor tiles the panel.** Under linuxfb the window *is* the framebuffer,
  so a factor that does not divide the panel exactly leaves an unpainted seam at
  the edge — 4.75 on a 4K panel is 2 px short across and 1 px over down. Every
  mode in the matrix tiles exactly except 768p, where the nearest tiling factor
  is 1.0 and half a pixel is the cheaper price.
- **The canvas is big enough for the busiest step.** Judging a layout on the
  language step, which embedded mode always opens on, says almost nothing: it
  holds one combo box. `RPI_SCALING_STEP=WifiCustomization` renders a form-heavy
  page instead, which is how the 540-row requirement was found.
- **The EDID cannot change the outcome.** Ten rows feed the same 4K panel every
  shape of EDID we have seen fail in the field — absent, empty, zeroed, a
  nonsense 16 mm width, a corrupt header, a truncated block — and all of them
  agree on 4. Before, those rows split between 1× and 2×.

Both clamps are covered too: a 640×480 panel is narrower than the design canvas,
so the fit is 0.94 and the lower clamp holds it at 1.0 (the UI clips rather than
shrinking, as it does today), and an absurd 16384×8192 mode is held at 6.0
rather than applying a factor of 18.

Two rows record behaviour that looks incidental rather than intended:

- `dsi-status-unknown` — a connector reporting `unknown` instead of
  `connected` is skipped, so a panel that never asserts connection gets no
  scaling. DSI and LVDS panels do this.
- `interlaced-first-mode` — sysfs names interlaced modes `1920x1080i`, which
  the mode parser cannot read, so the connector is dropped entirely.

Both currently leave `QT_SCALE_FACTOR` unset, which Qt treats as 1.0.
