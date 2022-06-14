/*
 * Use GnuTLS for hashing as their code is more optimized than Qt's
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "acceleratedcryptographichash.h"

AcceleratedCryptographicHash::AcceleratedCryptographicHash(QCryptographicHash::Algorithm method)
{
    if (method != QCryptographicHash::Sha256)
        throw std::runtime_error("Only sha256 implemented");

    gnutls_hash_init(&_sha256, GNUTLS_DIG_SHA256);

}

AcceleratedCryptographicHash::~AcceleratedCryptographicHash()
{
    gnutls_hash_deinit(_sha256, NULL);
}

void AcceleratedCryptographicHash::addData(const char *data, int length)
{
    gnutls_hash(_sha256, data, length);
}

void AcceleratedCryptographicHash::addData(const QByteArray &data)
{
    addData(data.constData(), data.size());
}

QByteArray AcceleratedCryptographicHash::result()
{
    unsigned char binhash[gnutls_hash_get_len(GNUTLS_DIG_SHA256)];
    gnutls_hash_output(_sha256, binhash);
    return QByteArray((char *) binhash, sizeof binhash);
}
