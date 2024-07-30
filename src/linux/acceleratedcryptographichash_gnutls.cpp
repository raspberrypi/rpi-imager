/*
 * Use GnuTLS for SHA256
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2024 Raspberry Pi Ltd
 */

#include "acceleratedcryptographichash.h"
#include "gnutls/crypto.h"

struct AcceleratedCryptographicHash::impl {
    explicit impl(QCryptographicHash::Algorithm method)
    {
        if (method != QCryptographicHash::Sha256)
            throw std::runtime_error("Only sha256 implemented");

        gnutls_hash_init(&_sha256, GNUTLS_DIG_SHA256);

    }

    ~impl()
    {
        gnutls_hash_deinit(_sha256, NULL);
    }

    void addData(const char *data, int length)
    {
        gnutls_hash(_sha256, data, length);
    }

    void addData(const QByteArray &data)
    {
        addData(data.constData(), data.size());
    }

    QByteArray result()
    {
        unsigned char binhash[gnutls_hash_get_len(GNUTLS_DIG_SHA256)];
        gnutls_hash_output(_sha256, binhash);
        return QByteArray((char *) binhash, sizeof binhash);
    }

private:
    gnutls_hash_hd_t _sha256;
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
