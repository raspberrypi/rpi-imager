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
#include "openssl/sha.h"
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
    SHA256_CTX _sha256;
};

#endif // ACCELERATEDCRYPTOGRAPHICHASH_H
