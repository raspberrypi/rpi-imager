# How different versions of Raspberry Pi Imager customise different versions of Raspberry Pi OS

There have been several [issues](https://github.com/raspberrypi/rpi-imager/issues) getting confused by older versions of Raspberry Pi Imager not being able to customise newer versions of Raspberry Pi OS, so just for reference (and to avoid similar confusion in future) here's how it all works...

## Customisation formats

In the [online OS manifest](https://downloads.raspberrypi.com/os_list_imagingutility_v4.json) (this is the file that all versions of Raspberry Pi Imager download to display the list of available OSes), each operating sytem has additional metadata (see the [full reference](./json-schema/) and [additional guidance](./schema-notes.md)).
One of these metadata items is `init_format` and this tells Raspberry Pi Imager what style of customisation that OS image allows / expects.

The current options are:
 * `"init_format": "none"` - the OS image doesn't allow any customisation (this is also the implied option if `init_format` is missing)
 * `"init_format": "systemd"` - the OS image allows a "systemd" style customisation (using `firstrun.sh`)
 * `"init_format": "cloudinit"` - the OS image allows a standard "cloudinit" style customisation (this is used by some of the Ubuntu Server OS images)
 * `"init_format": "cloudinit-rpi"` - the OS image allows an enhanced "cloudinit-rpi" style customisation (more details available [here](https://www.raspberrypi.com/news/cloud-init-on-raspberry-pi-os/))

## Raspberry Pi Imager customisations

Raspberry Pi Imager 1.x only supports the `systemd` and `cloudinit` formats.

Raspberry Pi Imager 2.x supports the full range of `init_format` options: `none`, `systemd`, `cloudinit` & `cloudinit-rpi`.

## Raspberry Pi OS customisations

Raspberry Pi OS Bookworm images (and the initial 2025-10-01 release of Raspberry Pi OS Trixie) use `"init_format": "systemd"`.

The latest Raspberry Pi OS Trixie images use `"init_format": "cloudinit-rpi"`.

## Raspberry Pi Imager and Raspberry Pi OS combinations

Raspberry Pi Imager 1.x can successfully customise Raspberry Pi OS Bookworm images, but is unable to customise Raspberry Pi OS Trixie images.

Unfortunately, there's a bug in Imager 1.x whereby if it sees an `init_format` that it doesn't know about (or if the `init_format` is missing) it _assumes_ that the image allows `systemd` customisation.
This unfortunately means that if you try to use Imager 1.x to customise a Raspberry Pi OS Trixie image (which uses `"init_format": "cloudinit-rpi"`), Imager will incorrectly assume `"init_format": "systemd"`.
And therefore when you try booting this customised Trixie image, you'll find that none of your customisations have been applied, because the image was customised with the wrong method.

Raspberry Pi Imager 2.x is able to customise any version of Raspberry Pi OS.

## Customising locally-downloaded OS image files

Due to the aforementioned bug, if you load a locally-downloaded OS image file (typically with the file-extension `*.img` or `*.img.xz`) into Raspberry Pi Imager 1.x, it'll assume `"init_format": "systemd"`.
This means that Imager 1.x is able to succesfully customise a local Raspberry Pi OS Bookworm image.
But although Imager 1.x will _appear_ to customise a local Raspberry Pi OS Trixie image, that customisation will be ineffective.

If you try loading a locally-downloaded OS image file into Raspberry Pi Imager 2.x, it'll correctly assume `"init_format": "none"`, because it has no way of knowing which customisation format the image expects.
However you can use [create_local_json.py](./local_json/) to create a local OS manifest, which contains the required `init_format` metadata (which will then allow Imager 2.x to customise your local OS image).
