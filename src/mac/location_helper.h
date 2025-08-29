/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RPI_IMAGER_LOCATION_HELPER_H
#define RPI_IMAGER_LOCATION_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Requests When-In-Use location authorization if needed.
 * Returns 1 if authorized (or not required), 0 otherwise.
 */
int rpiimager_request_location_permission();

#ifdef __cplusplus
}
#endif

#endif /* RPI_IMAGER_LOCATION_HELPER_H */


