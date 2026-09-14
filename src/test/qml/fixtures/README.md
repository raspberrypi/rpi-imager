# QML test fixtures

## `os_list.json`

The repository document `qml_ui_test` reads instead of fetching one.

`qml_ui_test.cpp` points `RPI_IMAGER_OSLIST_URL` here unless the
environment already names a list, so no test reaches
`downloads.raspberrypi.com`. Without it the suite fetched the production
list on any run that clicked a Retry button -- which put the network
inside a seeded chaos sequence and made a storm's result depend on a CDN.

### How it was made

A capture of `os_list_imagingutility_v4.json`, with two changes:

* every remote `icon` replaced with `""` (264 of them), so nothing is
  fetched to draw the list;
* `imager.url` emptied, so the update check has nothing to reach.

The image `url` fields are left intact. They are only read when a write
starts, which the QML harness blocks three ways, and several cases need a
plausible URL and size to assert against.

### Refreshing it

Re-capture and re-apply both changes. Names matter: cases refer to
"Raspberry Pi OS (64-bit)", "Raspberry Pi OS Lite (64-bit)", "Raspberry
Pi OS (other)" and "Ubuntu Server", so a trimmed list will not do.

Icon *forms* are not covered here, deliberately -- every icon is empty.
The repository-side shapes are exercised by the hostile list in
`tst_chaos_oslist.qml` and by `fuzz_oslist`.
