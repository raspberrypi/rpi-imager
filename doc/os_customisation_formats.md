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

## Raspberry Pi Imager customisations

Raspberry Pi Imager 1.x only supports the `systemd` and `cloudinit` formats.

Raspberry Pi Imager 2.x supports the full range of `init_format` options: `none`, `systemd`, `cloudinit` and `cloudinit-rpi`.

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
