/*
 * Use Cryptography API: Next Generation for SHA256
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2024 Raspberry Pi Ltd
 */

#include <QDebug>

#include "acceleratedcryptographichash.h"

#include <windows.h>
#include <bcrypt.h>

#define NT_SUCCESS(Status)          (((NTSTATUS)(Status)) >= 0)

#define STATUS_UNSUCCESSFUL         ((NTSTATUS)0xC0000001L)

#ifdef ACCELERATED_HASH_ENABLE_TEST_API
// Make one CNG call report failure, chosen by position.
//
// The provider refusing is not something a test can arrange, and until it
// could, none of the six error paths below had ever run -- which is how they
// came to release the same buffers the destructor then released again. Each
// one is now walked deliberately.
namespace {
std::atomic<int> g_failAtCall{-1};
std::atomic<int> g_callOrdinal{0};

bool cngCallShouldFail()
{
    const int ordinal = g_callOrdinal.fetch_add(1);
    const int at = g_failAtCall.load();
    return at >= 0 && ordinal == at;
}
} // namespace

#define RPI_CNG(expr) (cngCallShouldFail() ? STATUS_UNSUCCESSFUL : (expr))
#else
#define RPI_CNG(expr) (expr)
#endif

struct AcceleratedCryptographicHash::impl {
    explicit impl(QCryptographicHash::Algorithm algo) {
        if (algo != QCryptographicHash::Sha256)
            throw std::runtime_error("Only sha256 implemented");

        //open an algorithm handle
        if(!NT_SUCCESS(status = RPI_CNG(BCryptOpenAlgorithmProvider(
                                                    &hAlg,
                                                    BCRYPT_SHA256_ALGORITHM,
                                                    NULL,
                                                    0))))
        {
            qDebug() << "BCryptOpenAlgorithmProvider returned Error " << status;
            cleanup();
            return;
        }

        //calculate the size of the buffer to hold the hash object
        if(!NT_SUCCESS(status = RPI_CNG(BCryptGetProperty(
                                            hAlg, 
                                            BCRYPT_OBJECT_LENGTH, 
                                            (PBYTE)&cbHashObject, 
                                            sizeof(DWORD), 
                                            &cbData, 
                                            0))))
        {
            qDebug() <<  "BCryptGetProperty returned Error " << status;
            cleanup();
            return;
        }

        //allocate the hash object on the heap
        pbHashObject = (PBYTE)HeapAlloc (GetProcessHeap(), 0, cbHashObject);
        if(NULL == pbHashObject)
        {
            qDebug() <<  "memory allocation failed";
            cleanup();
            return;
        }

    //calculate the length of the hash
        if(!NT_SUCCESS(status = RPI_CNG(BCryptGetProperty(
                                            hAlg, 
                                            BCRYPT_HASH_LENGTH, 
                                            (PBYTE)&cbHash, 
                                            sizeof(DWORD), 
                                            &cbData, 
                                            0))))
        {
            qDebug() << "BCryptGetProperty returned Error " << status;
            cleanup();
            return;
        }

        //allocate the hash buffer on the heap
        pbHash = (PBYTE)HeapAlloc (GetProcessHeap(), 0, cbHash);
        if(NULL == pbHash)
        {
            qDebug() <<  "memory allocation failed";
            cleanup();
            return;
        }

        //create a hash
        if(!NT_SUCCESS(status = RPI_CNG(BCryptCreateHash(
                                            hAlg, 
                                            &hHash, 
                                            pbHashObject, 
                                            cbHashObject, 
                                            NULL, 
                                            0, 
                                            0))))
        {
            qDebug() << "BCryptCreateHash returned Error " << status;
            cleanup();
            return;
        }
    }

    ~impl() {
        cleanup();
    }

    // Idempotent, and it has to be: every error path below calls this and
    // then returns, and the destructor calls it again. Without clearing what
    // it releases, a single CNG failure meant HeapFree twice on the same two
    // blocks and a second close of both handles -- heap corruption on the way
    // out of an object that had merely failed to hash.
    void cleanup() const {
        if(hAlg)
        {
            BCryptCloseAlgorithmProvider(hAlg,0);
            hAlg = NULL;
        }

        if (hHash)
        {
            BCryptDestroyHash(hHash);
            hHash = NULL;
        }

        if(pbHashObject)
        {
            HeapFree(GetProcessHeap(), 0, pbHashObject);
            pbHashObject = NULL;
        }

        if(pbHash)
        {
            HeapFree(GetProcessHeap(), 0, pbHash);
            pbHash = NULL;
        }
    }

    void addData(const char *data, int length)
    {
        //hash some data
        if(!NT_SUCCESS(status = RPI_CNG(BCryptHashData(
                                            hHash,
                                            (PBYTE)data,
                                            length,
                                            0))))
        {
            qDebug() << "BCryptHashData returned Error " << status;
            cleanup();
            return;
        }
    }

    void addData(const QByteArray &data)
    {
        addData(data.constData(), data.size());
    }

    QByteArray result() const {
            //close the hash
        if(!NT_SUCCESS(status = RPI_CNG(BCryptFinishHash(
                                            hHash, 
                                            pbHash, 
                                            cbHash, 
                                            0))))
        {
            qDebug() << "BCryptFinishHash returned Error " << status;
            cleanup();
            return {};
        } else {
            // No cleanup required, as the dtor of this class will do so.
            auto returnArray = QByteArray(reinterpret_cast<char *>(pbHash), cbHash);
            return returnArray;
        }
    }

private:
    // mutable because cleanup() is const: result() is const and releases on
    // failure.
    mutable BCRYPT_ALG_HANDLE   hAlg            = NULL;
    mutable BCRYPT_HASH_HANDLE  hHash           = NULL;
    mutable NTSTATUS            status          = STATUS_UNSUCCESSFUL;
    DWORD                       cbData          = 0,
                                cbHash          = 0,
                                cbHashObject    = 0;
    mutable PBYTE               pbHashObject    = NULL;
    mutable PBYTE               pbHash          = NULL;
};

AcceleratedCryptographicHash::AcceleratedCryptographicHash(QCryptographicHash::Algorithm method)
    : p_Impl(std::make_unique<impl>(method)), _algo(method) {}

AcceleratedCryptographicHash::~AcceleratedCryptographicHash() = default;

void AcceleratedCryptographicHash::addData(const char *data, int length) {
    p_Impl->addData(data, length);
}
void AcceleratedCryptographicHash::addData(const QByteArray &data) {
    p_Impl->addData(data);
}
QByteArray AcceleratedCryptographicHash::result() const {
    if (!_resultCached) {
        _cachedResult = p_Impl->result();
        _resultCached = true;
    }
    return _cachedResult;
}

#ifdef ACCELERATED_HASH_ENABLE_TEST_API
void AcceleratedCryptographicHash::failNextCngCallForTest(int ordinal) {
    g_failAtCall.store(ordinal);
    g_callOrdinal.store(0);
}

int AcceleratedCryptographicHash::cngCallCountForTest() {
    return g_callOrdinal.load();
}

void AcceleratedCryptographicHash::releaseTwiceForTest() {
    p_Impl->cleanup();
    p_Impl->cleanup();
}
#endif

void AcceleratedCryptographicHash::reset() {
    p_Impl = std::make_unique<impl>(_algo);
    _cachedResult.clear();
    _resultCached = false;
}