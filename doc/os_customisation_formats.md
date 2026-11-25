# Compatibility between different versions of Raspberry Pi Imager and Raspberry Pi OS

Several [issues](https://github.com/raspberrypi/rpi-imager/issues) have been reported when people get confused by older versions of Raspberry Pi Imager failing to customise newer versions of Raspberry Pi OS.
For reference, and to reduce confusion, the following describes how different versions of Raspberry Pi Imager apply OS customisation to different versions of Raspberry Pi OS.

## Customisation formats

Raspberry Pi Imager retrieves a list of available operating systems from the [online OS manifest](https://downloads.raspberrypi.com/os_list_imagingutility_v4.json).
Each operating system entry includes additional metadata; for a full reference and additional guidance see the [schema](./json-schema/) and [schema notes](./schema-notes.md).
The `init_format` metadata item tells Raspberry Pi Imager what style of customisation is expected and allowed by the OS image.

The currently supported methods are:
 * `"init_format": "none"` - the OS image doesn't support customisation; this is the default option if `init_format` is omitted
 * `"init_format": "systemd"` - the OS image allows a "systemd" style customisation (using `firstrun.sh`)
 * `"init_format": "cloudinit"` - the OS image allows a standard "cloudinit" style customisation (often used by Ubuntu Server OS images)
 * `"init_format": "cloudinit-rpi"` - the OS image allows an enhanced "cloudinit-rpi" style customisation; for more information, see [Cloud-init on Raspberry Pi OS](https://www.raspberrypi.com/news/cloud-init-on-raspberry-pi-os/)
 * `"init_format": "rpi-preseed"` - the OS image ships the [`rpi-preseed`](https://github.com/raspberrypi/rpi-preseed) package. Imager writes a single `rpi-preseed.toml` file to the FAT boot partition (read from `/boot/firmware/rpi-preseed.toml`), which `rpi-preseed` applies once on first boot. The TOML covers hostname, the operator account and password, SSH, Wi-Fi (written as a NetworkManager connection), locale, Raspberry Pi Connect enrolment and GPIO/hardware interfaces. Unlike the `systemd` and `cloudinit` formats, no `cmdline.txt` entry is added: `rpi-preseed`'s units are gated on the presence of the file.

## Raspberry Pi Imager customisations

Raspberry Pi Imager 1.x only supports the `systemd` and `cloudinit` formats.

Raspberry Pi Imager 2.x supports the full range of `init_format` options: `none`, `systemd`, `cloudinit`, `cloudinit-rpi` and `rpi-preseed`.

## Raspberry Pi OS customisations

Raspberry Pi OS Bookworm images (and the initial 2025-10-01 release of Raspberry Pi OS Trixie) use `"init_format": "systemd"`.

The latest Raspberry Pi OS Trixie images use `"init_format": "cloudinit-rpi"`.

## Raspberry Pi Imager and Raspberry Pi OS combinations

Raspberry Pi Imager 1.x can successfully customise Raspberry Pi OS Bookworm images, but is unable to customise Raspberry Pi OS Trixie images.

Unfortunately, there's a bug in Imager 1.x whereby if it sees an `init_format` that it doesn't know about (or if the `init_format` is missing) it _assumes_ that the image allows `systemd` customisation.
This means that if Imager 1.x is used to customise a Raspberry Pi OS Trixie image (which uses `"init_format": "cloudinit-rpi"`), Imager incorrectly assumes `"init_format": "systemd"`.
Therefore, when this customised Trixie image is booted, none of the customisations are applied because the image was customised with the wrong method.

Raspberry Pi Imager 2.x is able to customise any version of Raspberry Pi OS.

## Customising locally-downloaded OS image files

Due to the aforementioned bug, when loading a locally-downloaded OS image file (typically with the file-extension `*.img` or `*.img.xz`) into Raspberry Pi Imager 1.x, it assumes `"init_format": "systemd"`.
This means that Imager 1.x is able to successfully customise a local Raspberry Pi OS Bookworm image.
However although Imager 1.x will _appear_ to customise a local Raspberry Pi OS Trixie image, that customisation will be ineffective.

When loading a locally-downloaded OS image file into Raspberry Pi Imager 2.x, it correctly assumes `"init_format": "none"`, because it has no way of knowing which customisation format the image expects.
To enable customisation for local images in Imager 2.x, create a local OS manifest using [create_local_json.py](./local_json/).
This local OS manifest will then contain the required `init_format` metadata, which will allow Imager 2.x to successfully customise local OS images.

### Wi-Fi hotspots

The Wi-Fi customisation step offers **Join a network** and **Create a hotspot**.
The hotspot is an alternative to joining an existing network, rather than an
automatic fallback. It starts at boot, broadcasts the chosen SSID on `wlan0`
using the 2.4 GHz band, and gives connected devices addresses through
NetworkManager's shared IPv4 mode. The Pi is reachable at `10.42.0.1`.
Local access works without internet; internet sharing requires another upstream
connection on the Pi, such as Ethernet. Remote access still requires enabling
SSH or another service separately.

Hotspot creation requires an access-point-capable wireless adapter and
NetworkManager. Imager offers it for Raspberry Pi OS Bookworm or later with
`systemd`, `cloudinit-rpi`, or `rpi-preseed` customisation. Generic `cloudinit`
images and older Raspberry Pi OS releases do not offer it. A local `systemd`
image without a release date can use the option, but must include NetworkManager.

The generated `imager-hotspot.nmconnection` uses `mode=ap`, `ipv4.method=shared`,
autoconnect, and owner-only file permissions. Secure hotspots use WPA2 with a
derived 64-hex PSK; the wizard persists the derived key, never the plaintext
passphrase. Open hotspots omit the security section. Hotspot names are limited
to 32 UTF-8 bytes, and the hotspot always broadcasts its SSID.

For `systemd`, the first-run script installs the profile before the normal boot.
For `cloudinit-rpi`, `runcmd` installs and activates the profile while the
network-config retains Ethernet DHCP. For `rpi-preseed`, the native `[wlan]`
section keeps credentials in fields that preseed redacts from logs; an early
command converts its profile to a hotspot offline before NetworkManager starts.
