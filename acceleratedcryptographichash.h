#ifndef ACCELERATEDCRYPTOGRAPHICHASH_H
#define ACCELERATEDCRYPTOGRAPHICHASH_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

#include <QCryptographicHash>
#include "openssl/sha.h"

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
