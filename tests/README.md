Integration tests
===

Test if all json files in the public repository are correct (validate against json schema, and test all image files, icons and websites mentioned in the json do exist by performing HEAD requests)

```
$ cd tests
$ pytest

```

Test if a specific json file validates correctly

```
$ cd tests
$ pytest --repo=http://my-repo/os_list.json
```

Test image writes for all images in a repository

```
$ cd tests
$ truncate -s 16G loopfile
$ udisksctl loop-setup --file loopfile
Mapped file loopfile as /dev/loop24
$ sudo -g disk pytest test_write_images.py --repo=http://my-repo/os_list.json --device=/dev/loop24
```

Note: make sure automatic mounting of removable media is disabled in your Linux distribution during write tests.
You can also use real drives instead of loop files as device. But be very careful not to enter the wrong device. Writes are done for real, it is not a mock test...
