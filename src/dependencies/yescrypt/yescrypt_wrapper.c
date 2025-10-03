/*
 * SPDX-License-Identifier: Apache-2.0
 * C++ wrapper implementation for yescrypt password hashing
 */

#include "yescrypt_wrapper.h"
#include "yescrypt.h"
#include <string.h>
#include <stdio.h>

/* Buffer to hold the result - matches pattern from sha256crypt */
static char result_buffer[512];

char *yescrypt_crypt(const char *key, const char *salt)
{
    yescrypt_local_t local;
    uint8_t hash[256];
    uint8_t *retval;
    
    /* Initialize local data structure */
    if (yescrypt_init_local(&local) != 0) {
        return NULL;
    }
    
    /* The salt string is expected to be in the format "$y$j9T$..." 
     * where j9T specifies the parameters. If it's not in this format,
     * we'll create a proper salt string.
     */
    uint8_t setting[128];
    if (strncmp(salt, "$y$", 3) != 0 && strncmp(salt, "$7$", 3) != 0) {
        /* Need to generate a proper yescrypt setting string */
        yescrypt_params_t params = {
            .flags = YESCRYPT_RW | YESCRYPT_ROUNDS_6 | YESCRYPT_GATHER_4 | 
                     YESCRYPT_SIMPLE_2 | YESCRYPT_SBOX_12K,
            .N = 16384,  /* 2^14 - reasonable default */
            .r = 8,
            .p = 1,
            .t = 0,
            .g = 0,
            .NROM = 0
        };
        
        /* Use the provided salt as random bytes for encoding */
        retval = yescrypt_encode_params_r(&params, 
                                         (const uint8_t *)salt, 
                                         strlen(salt) < 16 ? strlen(salt) : 16,
                                         setting, 
                                         sizeof(setting));
        if (!retval) {
            yescrypt_free_local(&local);
            return NULL;
        }
    } else {
        /* Salt is already in the right format */
        snprintf((char *)setting, sizeof(setting), "%s", salt);
    }
    
    /* Compute the hash */
    retval = yescrypt_r(NULL, &local,
                       (const uint8_t *)key, strlen(key),
                       setting, NULL,
                       hash, sizeof(hash));
    
    if (!retval) {
        yescrypt_free_local(&local);
        return NULL;
    }
    
    /* Copy result to static buffer */
    snprintf(result_buffer, sizeof(result_buffer), "%s", (char *)hash);
    
    /* Clean up */
    yescrypt_free_local(&local);
    
    /* Clear sensitive data */
    memset(hash, 0, sizeof(hash));
    
    return result_buffer;
}

