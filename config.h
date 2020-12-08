#ifndef CONFIG_H
#define CONFIG_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */


/* Repository URL */
#define OSLIST_URL                        "https://downloads.raspberrypi.org/os_list_imagingutility_v2.json"

/* Time synchronization URL (only used on eglfs QPA platform, URL must be HTTP) */
#define TIME_URL                          "http://downloads.raspberrypi.org/os_list_imagingutility_v2.json?time_synchronization"

/* Phone home the name of images downloaded for image popularity ranking */
#define TELEMETRY_URL                     "https://rpi-imager-stats.raspberrypi.org/downloads"
#define TELEMETRY_ENABLED_DEFAULT         true

/* Hash algorithm for verifying (uncompressed image) checksum */
#define OSLIST_HASH_ALGORITHM             QCryptographicHash::Sha256

/* Hide system drives from list */
#define DRIVELIST_FILTER_SYSTEM_DRIVES    true

/* Update progressbar every 0.1 second */
#define PROGRESS_UPDATE_INTERVAL          100

/* Block size used for writes (currently used when using .zip images only) */
#define IMAGEWRITER_BLOCKSIZE             1*1024*1024

/* Block size used with uncompressed images */
#define IMAGEWRITER_UNCOMPRESSED_BLOCKSIZE 128*1024

/* Block size used when reading during verify stage */
#define IMAGEWRITER_VERIFY_BLOCKSIZE      128*1024

/* Enable caching */
#define IMAGEWRITER_ENABLE_CACHE_DEFAULT        true

/* Do not cache if it would bring free disk space under 5 GB */
#define IMAGEWRITER_MINIMAL_SPACE_FOR_CACHING   5*1024*1024*1024ll

#endif // CONFIG_H
