#ifndef ACCELERATEDCRYPTOGRAPHICHASH_H
#define ACCELERATEDCRYPTOGRAPHICHASH_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QCryptographicHash>
#include <memory>

struct AcceleratedCryptographicHash
{
private:
    struct impl;
    std::unique_ptr<impl> p_Impl;
    mutable QByteArray _cachedResult; // Cache the result to avoid multiple hash finalization calls
    mutable bool _resultCached = false;
    QCryptographicHash::Algorithm _algo;

public:
    explicit AcceleratedCryptographicHash(QCryptographicHash::Algorithm method);
    ~AcceleratedCryptographicHash();
    void addData(const char *data, int length);
    void addData(const QByteArray &data);
    QByteArray result() const;
    void reset();
};

#endif // ACCELERATEDCRYPTOGRAPHICHASH_H
