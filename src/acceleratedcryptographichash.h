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

#ifdef ACCELERATED_HASH_ENABLE_TEST_API
    // Release the backend's resources twice over.
    //
    // Every CNG error path releases and returns, and the destructor releases
    // again, so a single hardware failure used to free the same two heap
    // blocks twice. The failures themselves cannot be provoked from a test --
    // they are the card's crypto provider refusing -- but the sequence they
    // produce can be, and that is where the damage was.
    void releaseTwiceForTest();

    // Make the nth CNG call of the next operation report failure, counting
    // from zero, or -1 to stop. There are six, spread across construction,
    // addData() and result().
    static void failNextCngCallForTest(int ordinal);
    static int cngCallCountForTest();
#endif
};

#endif // ACCELERATEDCRYPTOGRAPHICHASH_H
