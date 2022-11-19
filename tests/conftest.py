import pytest
import json
import urllib.request

os_list_files = []
icon_urls = set()
website_urls = set()
item_json = []
already_processed_urls = set()
total_download_size = 0
largest_extract_size = 0

def pytest_addoption(parser):
    parser.addoption(
        "--repo",
        action="store",
        default="https://downloads.raspberrypi.com/os_list_imagingutility_v3.json",
        help="Repository URL to test"
    )
    parser.addoption(
        "--device",
        action="store",
        default="",
        help="(Loop) device if you want to perform actual image write tests"
    )

def parse_json_entries(j):
    global total_download_size, largest_extract_size

    for item in j:
        if "subitems" in item:
            parse_json_entries(item["subitems"])
        elif "subitems_url" in item:
            parse_os_list(item["subitems_url"])
        else:
            if "icon" in item and not item["icon"].startswith("data:"):
                icon_urls.add(item["icon"])
            if "website" in item:
                website_urls.add(item["website"])
            if "url" in item:
                item_json.append(item)
            if "image_download_size" in item:
                total_download_size += int(item["image_download_size"])
            if "extract_size" in item:
                largest_extract_size = max(largest_extract_size, int(item["extract_size"]))

def parse_os_list(url):
    if url in already_processed_urls:
        print("Circular reference! Already processed URL: {}".format(url))
        return

    already_processed_urls.add(url)

    try:
        print("Fetching OS list file {}".format(url))
        req = urllib.request.Request(
            url, 
            data=None, 
            headers={
                'User-Agent': 'rpi-imager automated tests'
            }
        )
        response = urllib.request.urlopen(req)
        body = response.read()
        j = json.loads(body)
        os_list_files.append( (url,body) )

        if "os_list" in j:
            parse_json_entries(j["os_list"])

    except Exception as err:
        print("Error processing '{}': {}".format(url, repr(err) ))

def pytest_configure(config):
    parse_os_list(config.getoption("--repo"))
    print("Found {} os_list.json files {} OS images {} icons {} website URLs".format( 
        len(os_list_files), len(item_json), len(icon_urls), len(website_urls) ) )
    print("Total compressed image download size: {} GB".format(round(total_download_size / 1024 ** 3) ))
    print("Largest uncompressed image size: {} GB".format(round(largest_extract_size / 1024 ** 3) ))


def pytest_generate_tests(metafunc):
    if "oslisttuple" in metafunc.fixturenames:
        metafunc.parametrize("oslisttuple", os_list_files)
    if "iconurl" in metafunc.fixturenames:
        metafunc.parametrize("iconurl", icon_urls)
    if "websiteurl" in metafunc.fixturenames:
        metafunc.parametrize("websiteurl", website_urls)
    if "imageitem" in metafunc.fixturenames:
        metafunc.parametrize("imageitem", item_json)
