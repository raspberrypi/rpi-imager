# Generate a local `os_list.json` file

TL;DR:
 * download some [OS images](https://www.raspberrypi.com/software/operating-systems/) into a directory
 * copy `create_local_json.py` into that same directory
 * run `create_local_json.py`
 * get an `os_list_local.json` that you can load into Imager

## Purpose

The latest version of Imager now supports several different methods of customising an Operating System disk image (_OS image_). To prevent things going awry when the wrong style of customisation is applied, Imager refuses to offer customisation options when it doesn't know what cusomisation method an image requires (the previous version of Imager would happily apply the "wrong" customisation method). When Imager starts up, it downloads a [manifest file](https://downloads.raspberrypi.com/os_list_imagingutility_v4.json) which tells it about all the online images available, as well as _metadata_ about those images, including what customisation methods those images require. So when flashing an online image onto an SD card, Imager knows what OS customisation options to display.

However when selecting a locally downloaded image file in Imager (the **Use custom** option), this metadata isn't available, and so Imager won't offer any customisation options. The workaround for this is to create a [JSON file](../schema-notes.md) for your local image file, containing the full path to your image file and any necessary metadata, and point Imager at this local JSON file. This process can be a bit cumbersome, so the `create_local_json.py` script has been created to make this a bit easier (for those OS images in Imager's current manifest file).

## How it works

When `create_local_json.py` runs, it downloads the same [manifest file](https://downloads.raspberrypi.com/os_list_imagingutility_v4.json) as Imager, and makes a note of all the (online) filenames it contains. It then searches for any local images that match these filenames, and creates an `os_list_local.json` file that contains the full path to these matching local images, as well as the metadata for these images copied from the online manifest. In Imager you can then select **App Options** > **Content Repository** > **EDIT** > **Use custom file** and select the new `os_list_local.json` file. After clicking **APPLY & RESTART**, the **OS** page in Imager shows your locally-downloaded images, and because the metadata is available, any OS customisation options are available too.

## Command line options

You can customise the behaviour of `create_local_json.py` with the following command line options:
  * `--repo REPO` - specify a custom manifest file URL to download
  * `--search-dir SEARCH_DIR` - search for local OS images in _SEARCH_DIR_ instead of the current directory
  * `--output-json OUTPUT_JSON` - write to _OUTPUT_JSON_ instead of `os_list_local.json`
  * `--dry-run` - don't create `os_list_local.json` but just show which matching local OS images were found
  * `--download-icons` - make a local copy of the device and OS icons referenced in the manifest file (this creates an `os_list_local.json` that can be used entirely offline)
  * `--verify-checksums` - check that any matching local filenames also match the `image_download_sha256` in the online manifest, before adding them to `os_list_local.json`

