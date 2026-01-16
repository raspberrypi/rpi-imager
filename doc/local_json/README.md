# Generate a local manifest file

TL;DR - to add capabilities (like USB Gadget mode) to official Raspberry Pi OS images:
```
./create_local_json.py --online --capabilities usb_otg
```
Then double-click `os_list_local.rpi-imager-manifest` to open it in Imager.

Or, if you want to use locally downloaded images:
```
./create_local_json.py --search-dir /path/to/images --capabilities usb_otg
```

## Purpose

The latest version of Imager now supports several different methods of customising an Operating System disk image (_OS image_). To prevent things going awry when the wrong style of customisation is applied, Imager refuses to offer customisation options when it doesn't know what cusomisation method an image requires (the previous version of Imager would happily apply the "wrong" customisation method). When Imager starts up, it downloads a [manifest file](https://downloads.raspberrypi.com/os_list_imagingutility_v4.json) which tells it about all the online images available, as well as _metadata_ about those images, including what customisation methods those images require. So when flashing an online image onto an SD card, Imager knows what OS customisation options to display.

However when selecting a locally downloaded image file in Imager (the **Use custom** option), this metadata isn't available, and so Imager won't offer any customisation options. The workaround for this is to create a [manifest file](../schema-notes.md) for your local image file, containing the full path to your image file and any necessary metadata, and point Imager at this local manifest file. This process can be a bit cumbersome, so the `create_local_json.py` script has been created to make this a bit easier (for those OS images in Imager's current manifest file).

## How it works

When `create_local_json.py` runs, it downloads the same [manifest file](https://downloads.raspberrypi.com/os_list_imagingutility_v4.json) as Imager, and makes a note of all the (online) filenames it contains. It then searches for any local images that match these filenames, and creates an `os_list_local.rpi-imager-manifest` file that contains the full path to these matching local images, as well as the metadata for these images copied from the online manifest.

To use the manifest, simply double-click the `os_list_local.rpi-imager-manifest` file to open it in Imager. The **OS** page will then show your locally-downloaded images, and because the metadata is available, any OS customisation options are available too.

Alternatively, you can select **App Options** > **Content Repository** > **EDIT** > **Use custom file** in Imager and select the manifest file, then click **APPLY & RESTART**. Or, if you want to _always_ use your local manifest, you can launch Imager with `rpi-imager --repo path/to/os_list_local.rpi-imager-manifest`.

## Command line options

You can customise the behaviour of `create_local_json.py` with the following command line options:
  * `--online` - use online URLs from the official repository instead of searching for local files (useful for adding capabilities without downloading images first)
  * `--repo REPO` - specify a custom manifest file URL to download
  * `--search-dir SEARCH_DIR` - search for local OS images in _SEARCH_DIR_ instead of the current directory
  * `--output-json OUTPUT_JSON` - write to _OUTPUT_JSON_ instead of `os_list_local.rpi-imager-manifest`
  * `--dry-run` - don't create the manifest but just show which OS images would be included
  * `--download-icons` - make a local copy of the device and OS icons referenced in the manifest file (this creates a manifest that can be used entirely offline)
  * `--verify-checksums` - check that any matching local filenames also match the `image_download_sha256` in the online manifest, before adding them to the output (ignored with `--online`)
  * `--capabilities CAP [CAP ...]` - add OS capabilities to enable features in the customization wizard

### Available capabilities

The `--capabilities` option accepts one or more of the following values:

| Capability | Description |
|------------|-------------|
| `i2c` | Enable I2C interface option |
| `onewire` | Enable 1-Wire interface option |
| `rpi_connect` | Enable Raspberry Pi Connect setup |
| `secure_boot` | Enable secure boot signing |
| `serial` | Enable serial interface option |
| `spi` | Enable SPI interface option |
| `usb_otg` | Enable USB Gadget mode option |

**Note:** Interface capabilities (`i2c`, `spi`, `onewire`, `serial`, `usb_otg`) require both hardware and OS support to be available in the customization wizard. The hardware capabilities are defined per-device in the manifest.

### Examples

Create a manifest with USB Gadget mode enabled for all official Raspberry Pi OS images (downloads from official servers):
```
./create_local_json.py --online --capabilities usb_otg
```

Enable multiple capabilities for locally downloaded images:
```
./create_local_json.py --capabilities usb_otg i2c spi
```

