/*
 * Use OpenSSL for hashing as their code is more optimized than Qt's
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "acceleratedcryptographichash.h"

AcceleratedCryptographicHash::AcceleratedCryptographicHash(QCryptographicHash::Algorithm method)
{
    if (method != QCryptographicHash::Sha256)
        throw std::runtime_error("Only sha256 implemented");

    SHA256_Init(&_sha256);
}

AcceleratedCryptographicHash::~AcceleratedCryptographicHash()
{
}

void AcceleratedCryptographicHash::addData(const char *data, int length)
{
    SHA256_Update(&_sha256, data, length);
}

void AcceleratedCryptographicHash::addData(const QByteArray &data)
{
    addData(data.constData(), data.size());
}

QByteArray AcceleratedCryptographicHash::result()
{
    unsigned char binhash[SHA256_DIGEST_LENGTH];
    SHA256_Final(binhash, &_sha256);
    return QByteArray((char *) binhash, sizeof binhash);
}
