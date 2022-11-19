import pytest
import re
import subprocess
import os
import time
from shlex import quote


def shell(cmd, url):
    msg = ''
    try:
        subprocess.run(cmd, shell=True, check=True, capture_output=True)
    except subprocess.CalledProcessError as err:
        msg = "{} Error running '{}' exit code {} stderr: '{}'".format(url, err.cmd, err.returncode, err.output)

    if msg != '':
        pytest.fail(msg, False)


def test_write_image_and_modify_fat(imageitem, device):
    if not device:
        pytest.skip("--device=<device> not specified. Skipping write tests")
        return

    assert "extract_sha256" in imageitem, "{}: missing extract_sha256. Cannot perform write test.".format(imageitem["url"])
    assert "image_download_size" in imageitem, "{}: missing image_download_size. Cannot perform write test.".format(imageitem["url"])
    assert re.search("^[a-z0-9]{64}$", imageitem["extract_sha256"]) != None

    cacheFile = "cache/"+imageitem["extract_sha256"]
    if os.path.exists(cacheFile) and os.path.getsize(cacheFile) != imageitem["image_download_size"]:
        os.remove(cacheFile)

    shell("rpi-imager --cli --quiet --enable-writing-system-drives --sha256 {} --cache-file {} --first-run-script test_firstrun.txt {} {}".format(
        quote(imageitem["extract_sha256"]), quote(cacheFile), quote(imageitem["url"]), quote(device) ), imageitem["url"])
    time.sleep(0.5)
    shell("fsck.vfat -n "+quote(device+"p1"), imageitem["url"])

@pytest.fixture
def device(request):
    return request.config.getoption("--device")
