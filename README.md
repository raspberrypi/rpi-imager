# imagewriter

Uses [Etcher's SDK](https://github.com/balena-io-modules/etcher-sdk) to do the
heavylifting of downloading/writing the image to the SD card.

Reads the list of operating systems from a JSON file currently hosted at
https://www.pibakery.org/imagewriter/os_list.json.

This application needs to run as root/admin. Upon launch by a non-admin/root
user, it will attempt to elevate itself.

To run in development mode, run `npm start`. If changes are made to the app whilst it is running, the app will *not* auto-reload, so it should be manually killed and restarted to see the changes.

To build, run `npm run package`. This will build the program into the
`build-out` directory.
