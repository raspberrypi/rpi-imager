#!/usr/bin/env python3
import argparse
import hashlib
import json
import os.path
import pathlib
import shutil
import sys
import urllib.request
from collections import OrderedDict

DEFAULT_REPO_URL = "https://downloads.raspberrypi.com/os_list_imagingutility_v4.json"
DEFAULT_OUTPUT_JSON_FILE = "os_list_local.rpi-imager-manifest"
CACHE_DIR = "cache"
ICONS_DIR = os.path.join(CACHE_DIR, "icons")
CHUNK_SIZE = 1024 * 1024 # read files in 1MB chunks

# Valid OS capabilities (see doc/json-schema/os-list-schema.json)
VALID_OS_CAPABILITIES = {
    "i2c":         "Enable I2C interface option",
    "onewire":     "Enable 1-Wire interface option",
    "rpi_connect": "Enable Raspberry Pi Connect setup",
    "secure_boot": "Enable secure boot signing",
    "serial":      "Enable serial interface option",
    "spi":         "Enable SPI interface option",
    "usb_otg":     "Enable USB Gadget mode option",
}

# Valid device (hardware) capabilities
VALID_DEVICE_CAPABILITIES = {
    "i2c":                   "Device supports I2C interface",
    "onewire":               "Device supports 1-Wire interface",
    "serial":                "Device supports serial interface",
    "serial_on_console_only": "Serial is console-only mode",
    "spi":                   "Device supports SPI interface",
    "usb_otg":               "Device supports USB Gadget mode",
}


def fatal_error(reason):
    print(f"Error: {reason}")
    sys.exit(1)

def display_warning(reason):
    print(f"Warning: {reason}")

def scan_os_list(os_list, os_entries=OrderedDict(), duplicates=set()):
    for os_entry in os_list:
        if "subitems" in os_entry:
            scan_os_list(os_entry["subitems"], os_entries, duplicates)
        elif "url" in os_entry:
            image_filename = os.path.basename(os_entry["url"])
            if image_filename not in duplicates:
                if image_filename in os_entries:
                    duplicates.add(image_filename)
                    del os_entries[image_filename]
                os_entries[image_filename] = os_entry
    return os_entries, duplicates

def download_file(url, filename):
    try:
        #print(f"Downloading {url} to {filename}")
        with urllib.request.urlopen(url) as response:
            with open(filename, "wb") as fh:
                shutil.copyfileobj(response, fh, CHUNK_SIZE)
                return True
    except Exception:
        return False

def download_icon(icon_url):
    icon_filename = os.path.join(ICONS_DIR, os.path.basename(icon_url))
    if os.path.exists(icon_filename) or download_file(icon_url, icon_filename):
        return icon_filename
    else:
        display_warning(f"Couldn't download {icon_url}")

def calculate_checksum(filename):
    with open(filename, "rb") as f:
        if hasattr(hashlib, "file_digest"):
            # only available in python 3.11 or newer
            hasher = hashlib.file_digest(f, "sha256")
        else:
            hasher = hashlib.new("sha256")
            while True:
                data = f.read(CHUNK_SIZE)
                if not data:
                    break
                hasher.update(data)
        return hasher.hexdigest()


if __name__ == "__main__":
    epilog = "available OS capabilities for --capabilities:\n"
    epilog += "\n".join(f"  {cap:12}  {desc}" for cap, desc in VALID_OS_CAPABILITIES.items())
    epilog += "\n\navailable device capabilities for --device-capabilities:\n"
    epilog += "\n".join(f"  {cap:20}  {desc}" for cap, desc in VALID_DEVICE_CAPABILITIES.items())

    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=epilog)
    parser.add_argument("--repo", default=DEFAULT_REPO_URL, help="custom repository URL")
    parser.add_argument("--search-dir", default=".", help="directory to search for downloaded images")
    parser.add_argument("--output-json", default=DEFAULT_OUTPUT_JSON_FILE, help=f"manifest file to create (defaults to {DEFAULT_OUTPUT_JSON_FILE})")
    parser.add_argument("--online", action="store_true", help="use online URLs from the repository instead of searching for local files")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--dry-run", action="store_true", help="dry run only, don't create manifest file")
    group.add_argument("--download-icons", action="store_true", help="make a local copy of the device and OS icons")
    parser.add_argument("--verify-checksums", action="store_true", help="verify the checksums of the downloaded images (ignored with --online)")
    parser.add_argument("--capabilities", nargs="+", metavar="CAP",
                        help="add OS capabilities to enable features in the customization wizard (see below)")
    parser.add_argument("--device-capabilities", nargs="+", metavar="CAP",
                        help="add device (hardware) capabilities to all devices (see below)")

    args = parser.parse_args()

    # Validate OS capabilities if provided
    if args.capabilities:
        invalid_caps = set(args.capabilities) - VALID_OS_CAPABILITIES.keys()
        if invalid_caps:
            fatal_error(f"Invalid OS capabilities: {', '.join(sorted(invalid_caps))}. Valid capabilities are: {', '.join(VALID_OS_CAPABILITIES.keys())}")

    # Validate device capabilities if provided
    if args.device_capabilities:
        invalid_caps = set(args.device_capabilities) - VALID_DEVICE_CAPABILITIES.keys()
        if invalid_caps:
            fatal_error(f"Invalid device capabilities: {', '.join(sorted(invalid_caps))}. Valid capabilities are: {', '.join(VALID_DEVICE_CAPABILITIES.keys())}")

    repo_urlparts = urllib.parse.urlparse(args.repo)
    if not repo_urlparts.netloc or repo_urlparts.scheme not in ("http", "https"):
        fatal_error("Expected repo to be a http:// or https:// URL")

    repo_filename = os.path.basename(repo_urlparts.path)
    if os.path.exists(repo_filename):
        online_json_file = repo_filename
    else:
        os.makedirs(CACHE_DIR, exist_ok=True)
        online_json_file = os.path.join(CACHE_DIR, repo_filename)
    if os.path.exists(online_json_file):
        print(f"Info: {online_json_file} already exists, so using local file")
    else:
        if not download_file(args.repo, online_json_file):
            fatal_error(f"Couldn't download {args.repo}")

    with open(online_json_file) as online_fh:
        online_data = json.load(online_fh)
    # Find image filenames in the online JSON (discarding duplicates)
    online_os_entries, duplicate_online_filenames = scan_os_list(online_data["os_list"])
    if not online_os_entries:
        fatal_error(f"No matching OSes found in {online_json_file}")
    if args.download_icons:
        os.makedirs(ICONS_DIR, exist_ok=True)

    local_os_entries = dict()
    local_device_tags = set()

    if args.online:
        # Use all OS entries from online manifest with their original URLs
        for name, os_entry in online_os_entries.items():
            if args.dry_run:
                print(f"Including {os_entry['name']}")
            local_os_entries[name] = os_entry
            if "devices" in os_entry:
                for tag in os_entry["devices"]:
                    local_device_tags.add(tag)
            if args.download_icons and "icon" in os_entry:
                local_icon_filename = download_icon(os_entry["icon"])
                if local_icon_filename:
                    os_entry["icon"] = pathlib.Path(os.path.abspath(local_icon_filename)).as_uri()
    else:
        # Recursively search for any matching local filenames
        duplicate_local_filenames = set()
        abs_search_dir_len = len(os.path.abspath(args.search_dir)) + 1
        for root, dirs, files in os.walk(args.search_dir):
            for name in files:
                abs_local_image_filename = os.path.abspath(os.path.join(root, name))
                # remove leading search_dir prefix
                display_filename = abs_local_image_filename[abs_search_dir_len:]
                if name in duplicate_online_filenames:
                    display_warning(f"Ignoring {display_filename} as it matched multiple filenames in {online_json_file}")
                elif name in duplicate_local_filenames:
                    pass
                elif name in local_os_entries:
                    display_warning(f"Ignoring {display_filename} as there are multiple local copies")
                    del local_os_entries[name]
                    duplicate_local_filenames.add(name)
                elif name in online_os_entries:
                    os_entry = online_os_entries[name]
                    if "image_download_size" in os_entry and os.path.getsize(abs_local_image_filename) != os_entry["image_download_size"]:
                        display_warning(f"Ignoring {display_filename} as its size doesn't match with {online_json_file}")
                    elif args.verify_checksums and "image_download_sha256" in os_entry and calculate_checksum(abs_local_image_filename) != os_entry["image_download_sha256"]:
                        display_warning(f"Ignoring {display_filename} as its checksum doesn't match with {online_json_file}")
                    else:
                        if args.dry_run:
                            print(f"Found {display_filename} ({os_entry['name']})")
                        # point at our local file instead of the online URL
                        os_entry["url"] = pathlib.Path(abs_local_image_filename).as_uri()
                        local_os_entries[name] = os_entry
                        if "devices" in os_entry:
                            for tag in os_entry["devices"]:
                                local_device_tags.add(tag)
                        if args.download_icons and "icon" in os_entry:
                            local_icon_filename = download_icon(os_entry["icon"])
                            if local_icon_filename:
                                os_entry["icon"] = pathlib.Path(os.path.abspath(local_icon_filename)).as_uri()

    num_images = len(local_os_entries)
    if num_images < 1:
        if args.dry_run:
            fatal_error(f"No matching image files found")
        else:
            fatal_error(f"No matching image files found, so not creating {args.output_json}")
    # Create the output data structure
    local_data = dict()
    if "imager" in online_data and "devices" in online_data["imager"]:
        local_data["imager"] = dict()
        local_data["imager"]["devices"] = list()
        for device in online_data["imager"]["devices"]:
            if "tags" not in device or not device["tags"] or any(tag in local_device_tags for tag in device["tags"]):
                local_data["imager"]["devices"].append(device)
                if args.download_icons and "icon" in device:
                    local_icon_filename = download_icon(device["icon"])
                    if local_icon_filename:
                        device["icon"] = pathlib.Path(os.path.abspath(local_icon_filename)).as_uri()
                # Add device capabilities if specified
                if args.device_capabilities:
                    existing_caps = set(device.get("capabilities", []))
                    merged_caps = existing_caps | set(args.device_capabilities)
                    device["capabilities"] = sorted(merged_caps)
    # Sort the output OSes into the same order as the input OSes
    os_order = list(online_os_entries.keys())
    local_data["os_list"] = sorted(local_os_entries.values(), key=lambda x: os_order.index(os.path.basename(x["url"])))

    # Add capabilities to OS entries if specified
    if args.capabilities:
        for os_entry in local_data["os_list"]:
            # Merge with existing capabilities if present
            existing_caps = set(os_entry.get("capabilities", []))
            merged_caps = existing_caps | set(args.capabilities)
            os_entry["capabilities"] = sorted(merged_caps)
    # And finally write the local JSON file
    if args.dry_run:
        print(f"Would write {num_images} image{'s' if num_images > 1 else ''} to {args.output_json}")
    else:
        with open(args.output_json, "w") as local_fh:
            json.dump(local_data, local_fh, indent=2)
        print(f"Wrote {num_images} image{'s' if num_images > 1 else ''} to {args.output_json}")

