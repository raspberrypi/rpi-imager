/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RPI_IMAGER_LOCATION_HELPER_H
#define RPI_IMAGER_LOCATION_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Callback type for location permission changes.
 * Called with 1 when authorized, 0 when denied/restricted.
 */
typedef void (*rpiimager_location_callback)(int authorized, void *context);

/* Requests When-In-Use location authorization if needed.
 * Returns 1 if authorized (or not required), 0 otherwise.
 * 
 * If callback is non-NULL and permission is not yet determined,
 * the callback will be invoked when permission is granted/denied
 * (even if this happens after the timeout).
 */
int rpiimager_request_location_permission_async(rpiimager_location_callback callback, void *context);

/* Legacy synchronous version (no callback) */
int rpiimager_request_location_permission();

/* Check current authorization status without prompting.
 * Returns 1 if authorized, 0 otherwise.
 */
int rpiimager_check_location_permission();

#ifdef __cplusplus
}
#endif

#endif /* RPI_IMAGER_LOCATION_HELPER_H */


