#ifndef ACCELERATEDCRYPTOGRAPHICHASH_H
#define ACCELERATEDCRYPTOGRAPHICHASH_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QCryptographicHash>

#ifdef Q_OS_DARWIN
#include <CommonCrypto/CommonDigest.h>
#define SHA256_CTX    CC_SHA256_CTX
#define SHA256_DIGEST_LENGTH  CC_SHA256_DIGEST_LENGTH
#define SHA256_Init   CC_SHA256_Init
#define SHA256_Update CC_SHA256_Update
#define SHA256_Final  CC_SHA256_Final
#else
#ifdef HAVE_GNUTLS
#include "gnutls/crypto.h"
#else
#include "openssl/sha.h"
#endif
#endif

class AcceleratedCryptographicHash
{
public:
    explicit AcceleratedCryptographicHash(QCryptographicHash::Algorithm method);
    virtual ~AcceleratedCryptographicHash();
    void addData(const char *data, int length);
    void addData(const QByteArray &data);
    QByteArray result();

protected:
#ifdef HAVE_GNUTLS
    gnutls_hash_hd_t _sha256;
#else
    SHA256_CTX _sha256;
#endif
};

#endif // ACCELERATEDCRYPTOGRAPHICHASH_H
