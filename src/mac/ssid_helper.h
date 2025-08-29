/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RPI_IMAGER_SSID_HELPER_H
#define RPI_IMAGER_SSID_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a heap-allocated UTF-8 C string with the current WiFi SSID, or NULL if unavailable.
 * Caller must free() the returned pointer.
 */
const char *rpiimager_current_ssid_cstr();

#ifdef __cplusplus
}
#endif

#endif /* RPI_IMAGER_SSID_HELPER_H */


