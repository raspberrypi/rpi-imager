/*
 * SPDX-License-Identifier: Apache-2.0
 * C++ wrapper for yescrypt password hashing
 * 
 * This is a simple wrapper around the Openwall yescrypt implementation
 * for use in rpi-imager. The yescrypt library is from:
 * https://github.com/openwall/yescrypt
 */

#ifndef YESCRYPT_WRAPPER_H
#define YESCRYPT_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

/* Simple wrapper function for password hashing with yescrypt.
 * This generates a yescrypt hash similar to how sha256_crypt works.
 * 
 * key: the password to hash
 * salt: a salt string (we'll generate the full yescrypt salt format)
 * 
 * Returns: a statically allocated string with the hash, or NULL on error
 */
char *yescrypt_crypt(const char *key, const char *salt);

#ifdef __cplusplus
}
#endif

#endif /* YESCRYPT_WRAPPER_H */

