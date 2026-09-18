/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Storage for a secret that can actually be cleared.
 *
 * The WLAN passphrase was held in a QByteArray and wiped in the destructor
 * with SecureZeroMemory(_psk.data(), _psk.size()). QByteArray is implicitly
 * shared and its non-const data() detaches: once the passphrase had been
 * handed to a caller, that call allocated a fresh buffer, copied the secret
 * into it, and zeroed the copy. The original was left exactly where it was,
 * and the code read as though it had been cleared.
 */

#include <catch2/catch_test_macros.hpp>

#include "secure_bytes.h"

#include <QByteArray>

#include <cstring>
#include <string>

using rpi_imager::SecureBytes;
using rpi_imager::secureZero;

namespace {

bool allZero(const char *p, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)
        if (p[i] != 0)
            return false;
    return true;
}

} // namespace

TEST_CASE("Wiping clears the bytes that were actually held", "[securebytes]")
{
    SecureBytes secret;
    secret.assign(QByteArrayLiteral("hunter2-correct-horse"));
    REQUIRE(secret.size() == 21);

    // The storage itself, before and after. This is the check the old code
    // could not have passed: it wiped a copy made for the occasion.
    const char *storage = secret.data();
    REQUIRE(storage != nullptr);
    REQUIRE_FALSE(allZero(storage, secret.size()));

    const std::size_t held = secret.size();
    std::string before(storage, held);
    secret.wipe();

    CHECK(secret.empty());
    CHECK(secret.size() == 0);
    CHECK(before == "hunter2-correct-horse");   // it really was there
}

TEST_CASE("A copy handed out shares nothing with the storage", "[securebytes]")
{
    // The property the wipe depends on. If the copy shared, wiping would
    // either corrupt the caller's data or -- as it did -- silently detach
    // and leave the secret behind.
    SecureBytes secret;
    secret.assign(QByteArrayLiteral("passphrase"));

    const QByteArray given = secret.copy();
    CHECK(given == QByteArrayLiteral("passphrase"));
    CHECK(given.constData() != secret.data());

    secret.wipe();

    // Ours is gone; theirs is untouched, which is the caller's to manage.
    CHECK(secret.empty());
    CHECK(given == QByteArrayLiteral("passphrase"));
}

TEST_CASE("Each copy is its own", "[securebytes]")
{
    SecureBytes secret;
    secret.assign(QByteArrayLiteral("passphrase"));

    const QByteArray a = secret.copy();
    const QByteArray b = secret.copy();
    CHECK(a == b);
    CHECK(a.constData() != b.constData());
}

TEST_CASE("Assigning again clears what was there first", "[securebytes]")
{
    // A second network's passphrase must not leave the first one behind.
    SecureBytes secret;
    secret.assign(QByteArrayLiteral("first-network-key"));
    secret.assign(QByteArrayLiteral("second"));

    CHECK(secret.copy() == QByteArrayLiteral("second"));
    CHECK(secret.size() == 6);
}

TEST_CASE("Wiping twice, and wiping nothing, are both harmless", "[securebytes]")
{
    // The destructor wipes, and a caller may have wiped already.
    SecureBytes empty;
    CHECK_NOTHROW(empty.wipe());
    CHECK_NOTHROW(empty.wipe());
    CHECK(empty.empty());
    CHECK(empty.data() == nullptr);
    CHECK(empty.copy().isEmpty());

    SecureBytes secret;
    secret.assign(QByteArrayLiteral("key"));
    secret.wipe();
    CHECK_NOTHROW(secret.wipe());
    CHECK(secret.empty());
}

TEST_CASE("Assigning nothing holds nothing", "[securebytes]")
{
    SecureBytes secret;
    secret.assign(QByteArray());
    CHECK(secret.empty());
    secret.assign(nullptr, 0);
    CHECK(secret.empty());
    secret.assign("ignored", 0);
    CHECK(secret.empty());
}

TEST_CASE("The zeroing helper clears exactly what it was given",
          "[securebytes]")
{
    // Bounded: a wipe that ran past its buffer would clear whatever followed
    // it, and one that stopped short would leave part of the secret.
    char buffer[16];
    std::memset(buffer, 0xAB, sizeof(buffer));

    secureZero(buffer + 4, 8);

    CHECK(buffer[3] == static_cast<char>(0xAB));
    CHECK(allZero(buffer + 4, 8));
    CHECK(buffer[12] == static_cast<char>(0xAB));

    // And the answers that must not fault.
    CHECK_NOTHROW(secureZero(nullptr, 16));
    CHECK_NOTHROW(secureZero(buffer, 0));
}
