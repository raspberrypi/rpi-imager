import urllib.request

def _head_request(url):
    req = urllib.request.Request(
        url, 
        data=None, 
        headers={
            'User-Agent': 'rpi-imager automated tests'
        },
        method="HEAD"
    )
    return urllib.request.urlopen(req)    


def test_icon_url_exists(iconurl):
    assert _head_request(iconurl).status == 200

def test_website_url_exists(websiteurl):
    assert _head_request(websiteurl).status == 200

def test_image_url_exists_and_has_correct_image_download_size(imageitem):
    response = _head_request(imageitem["url"])

    assert response.status == 200
    assert str(imageitem["image_download_size"]) == response.headers["Content-Length"]
