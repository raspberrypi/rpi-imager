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

struct AcceleratedCryptographicHash::impl {
    explicit impl(QCryptographicHash::Algorithm algo) {
        if (algo != QCryptographicHash::Sha256)
            throw std::runtime_error("Only sha256 implemented");

        //open an algorithm handle
        if(!NT_SUCCESS(status = BCryptOpenAlgorithmProvider(
                                                    &hAlg,
                                                    BCRYPT_SHA256_ALGORITHM,
                                                    NULL,
                                                    0)))
        {
            qDebug() << "BCryptOpenAlgorithmProvider returned Error " << status;
            cleanup();
            return;
        }

        //calculate the size of the buffer to hold the hash object
        if(!NT_SUCCESS(status = BCryptGetProperty(
                                            hAlg, 
                                            BCRYPT_OBJECT_LENGTH, 
                                            (PBYTE)&cbHashObject, 
                                            sizeof(DWORD), 
                                            &cbData, 
                                            0)))
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
        if(!NT_SUCCESS(status = BCryptGetProperty(
                                            hAlg, 
                                            BCRYPT_HASH_LENGTH, 
                                            (PBYTE)&cbHash, 
                                            sizeof(DWORD), 
                                            &cbData, 
                                            0)))
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
        if(!NT_SUCCESS(status = BCryptCreateHash(
                                            hAlg, 
                                            &hHash, 
                                            pbHashObject, 
                                            cbHashObject, 
                                            NULL, 
                                            0, 
                                            0)))
        {
            qDebug() << "BCryptCreateHash returned Error " << status;
            cleanup();
            return;
        }
    }

    ~impl() {
        cleanup();
    }

    void cleanup() {
        if(hAlg)
        {
            BCryptCloseAlgorithmProvider(hAlg,0);
        }

        if (hHash)    
        {
            BCryptDestroyHash(hHash);
        }

        if(pbHashObject)
        {
            HeapFree(GetProcessHeap(), 0, pbHashObject);
        }

        if(pbHash)
        {
            HeapFree(GetProcessHeap(), 0, pbHash);
        } 
    }

    void addData(const char *data, int length)
    {
        //hash some data
        if(!NT_SUCCESS(status = BCryptHashData(
                                            hHash,
                                            (PBYTE)data,
                                            length,
                                            0)))
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

    QByteArray result() {
            //close the hash
        if(!NT_SUCCESS(status = BCryptFinishHash(
                                            hHash, 
                                            pbHash, 
                                            cbHash, 
                                            0)))
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
    BCRYPT_ALG_HANDLE       hAlg            = NULL;
    BCRYPT_HASH_HANDLE      hHash           = NULL;
    NTSTATUS                status          = STATUS_UNSUCCESSFUL;
    DWORD                   cbData          = 0,
                            cbHash          = 0,
                            cbHashObject    = 0;
    PBYTE                   pbHashObject    = NULL;
    PBYTE                   pbHash          = NULL;
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