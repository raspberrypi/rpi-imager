/*
 * Use CommonCrypto on macOS for SHA256
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2024 Raspberry Pi Ltd
 */

#include "acceleratedcryptographichash.h"

#include <CommonCrypto/CommonDigest.h>

struct AcceleratedCryptographicHash::impl {
    explicit impl(QCryptographicHash::Algorithm algo) {
        if (algo != QCryptographicHash::Sha256)
            throw std::runtime_error("Only sha256 implemented");

        CC_SHA256_Init(&_sha256);
    }

    void addData(const char *data, int length)
    {
        CC_SHA256_Update(&_sha256, data, length);
    }

    void addData(const QByteArray &data)
    {
        addData(data.constData(), data.size());
    }

    QByteArray result() {
        unsigned char binhash[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256_Final(binhash, &_sha256);
        return QByteArray((char *) binhash, sizeof binhash);
    }

private:
    CC_SHA256_CTX _sha256;
};

AcceleratedCryptographicHash::AcceleratedCryptographicHash(QCryptographicHash::Algorithm method)
    : p_Impl(std::make_unique<impl>(method)) {}

AcceleratedCryptographicHash::~AcceleratedCryptographicHash() = default;

void AcceleratedCryptographicHash::addData(const char *data, int length) {
    p_Impl->addData(data, length);
}
void AcceleratedCryptographicHash::addData(const QByteArray &data) {
    p_Impl->addData(data);
}
QByteArray AcceleratedCryptographicHash::result() {
    return p_Impl->result();
}
