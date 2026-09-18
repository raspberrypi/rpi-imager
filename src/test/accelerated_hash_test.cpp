/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The hash that decides whether a written card is trusted.
 *
 * Every image is hashed twice: once from the bytes handed to the card, once
 * from the bytes read back off it, and the two digests are compared. If this
 * class ever disagreed with the reference implementation, or answered
 * differently depending on how the data arrived, that comparison would stop
 * meaning anything -- and it would stop quietly, because a wrong digest
 * compared against another wrong digest still matches.
 *
 * Note that result() spends the underlying context: the digest is cached so
 * that repeated calls agree, and reset() is the only supported way to hash a
 * second image with the same object.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "acceleratedcryptographichash.h"

#include <QByteArray>
#include <QCryptographicHash>

namespace {

QByteArray reference(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

} // namespace

TEST_CASE("The accelerated hash agrees with Qt's own SHA-256", "[hash]")
{
    // The verification the user relies on is only as good as the agreement
    // between this implementation and the one everything else uses. Three
    // shapes: nothing at all, one byte, and a string long enough to cross an
    // internal block boundary.
    const QByteArray cases[] = {
        QByteArray(),
        QByteArray("a"),
        QByteArray("The quick brown fox jumps over the lazy dog, repeatedly and at length."),
    };

    for (const QByteArray &data : cases) {
        AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(data);
        CHECK(hash.result().toHex() == reference(data));
    }
}

TEST_CASE("Data fed in pieces hashes the same as data fed at once", "[hash]")
{
    // The write path feeds whole buffers; the verify path feeds a first block
    // and then whatever each read returns. Those are different chunk sizes
    // over the same bytes, and the two digests are compared against each
    // other, so chunking must not change the answer.
    QByteArray payload;
    payload.reserve(1024 * 1024);
    for (int i = 0; i < 1024 * 1024; ++i)
        payload.append(static_cast<char>(i * 7 + (i >> 8)));

    AcceleratedCryptographicHash whole(QCryptographicHash::Sha256);
    whole.addData(payload);

    // Deliberately uneven, and none of them a divisor of the total.
    const int chunkSizes[] = { 1, 3, 4095, 65536, 262144 };
    AcceleratedCryptographicHash pieces(QCryptographicHash::Sha256);
    int offset = 0;
    int which = 0;
    while (offset < payload.size()) {
        const int take = qMin(chunkSizes[which % 5], payload.size() - offset);
        pieces.addData(payload.constData() + offset, take);
        offset += take;
        ++which;
    }

    CHECK(pieces.result().toHex() == whole.result().toHex());
    CHECK(whole.result().toHex() == reference(payload));
}

TEST_CASE("Taking the result twice gives the same answer", "[hash]")
{
    // The verify step reads the write digest four times: once to compare, and
    // again to put both figures in the message it logs. If the second read
    // differed from the first, a card that verified correctly could still be
    // reported as mismatched, with two digests that do not explain why.
    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray("some image bytes"));

    const QByteArray first = hash.result().toHex();
    CHECK(hash.result().toHex() == first);
    CHECK(hash.result().toHex() == first);
    CHECK(first == reference(QByteArray("some image bytes")));
}

TEST_CASE("reset() makes the object usable for a second image", "[hash]")
{
    // Taking the result finalises the context, so an object that has answered
    // once cannot simply be fed more data. reset() is the way back, and it has
    // to clear both the context and the cached digest: clearing only one would
    // return the previous image's hash for the next image.
    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);

    hash.addData(QByteArray("first image"));
    const QByteArray firstDigest = hash.result().toHex();
    CHECK(firstDigest == reference(QByteArray("first image")));

    hash.reset();
    hash.addData(QByteArray("second image"));
    const QByteArray secondDigest = hash.result().toHex();

    CHECK(secondDigest == reference(QByteArray("second image")));
    CHECK(secondDigest != firstDigest);
}

TEST_CASE("Asking for an algorithm that is not implemented is refused", "[hash]")
{
    // Only SHA-256 is wired up on any platform. The refusal matters because
    // the GnuTLS backend sizes its output buffer for SHA-256 unconditionally:
    // a backend that accepted SHA-1 here would write the wrong length.
    CHECK_THROWS(AcceleratedCryptographicHash(QCryptographicHash::Sha1));
    CHECK_THROWS(AcceleratedCryptographicHash(QCryptographicHash::Md5));
}

#if defined(Q_OS_WIN) && defined(ACCELERATED_HASH_ENABLE_TEST_API)
TEST_CASE("Releasing the backend twice does not corrupt the heap",
          "[hash][windows]")
{
    // Each of the eight CNG error paths releases and returns, and the
    // destructor releases again. Release did not clear what it had given
    // back, so one refusal from the provider meant HeapFree twice on the same
    // two blocks and a second close of both handles -- heap corruption on the
    // way out of an object that had only failed to hash.
    //
    // A double free does not return a value to check. Either this runs to the
    // end or the process does not survive it.
    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("something to allocate the buffers for"));
    CHECK(hash.result().size() == 32);

    CHECK_NOTHROW(hash.releaseTwiceForTest());
    SUCCEED("released twice and the heap is intact");
}
#endif

#if defined(Q_OS_WIN) && defined(ACCELERATED_HASH_ENABLE_TEST_API)

// ── what happens when the provider refuses ──────────────────────────────────
//
// Six CNG calls stand between a caller and a hash, and every one of them can
// fail: the provider is a system component, and on a locked-down or an
// FIPS-configured machine it does. Until these could be provoked none of the
// six error paths had ever run, and they were wrong -- each released the
// buffers that the destructor then released again.
//
// What is asked of each is the same: say so, hand back nothing, and come
// apart cleanly. A hash that quietly returned a wrong answer here would be
// written to the card and verified against itself.

namespace {

// Restores the injection whatever the case does, so one failure does not
// leak into the next case.
struct CngFailure
{
    explicit CngFailure(int ordinal)
    {
        AcceleratedCryptographicHash::failNextCngCallForTest(ordinal);
    }
    ~CngFailure() { AcceleratedCryptographicHash::failNextCngCallForTest(-1); }
    CngFailure(const CngFailure &) = delete;
    CngFailure &operator=(const CngFailure &) = delete;
};

} // namespace

TEST_CASE("A hash refused at any stage yields nothing and unwinds cleanly",
          "[hash][windows][cng]")
{
    // Walked one call at a time. Construction takes four, addData one and
    // result one, so this covers every refusal the provider can give.
    const int ordinal = GENERATE(0, 1, 2, 3, 4, 5);
    INFO("failing CNG call number " << ordinal);

    CngFailure fail(ordinal);

    // Construction must not throw whichever call is refused: the caller is a
    // write already under way, and an exception here would come out of a
    // thread with no handler for it.
    CHECK_NOTHROW([&] {
        AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayLiteral("payload that will not be hashed"));
        const QByteArray result = hash.result();
        // Either the hash is right or there is none. A short or truncated
        // digest written to the card would verify against itself and pass.
        CHECK((result.isEmpty() || result.size() == 32));
    }());
}

TEST_CASE("A refusal does not stop the object being destroyed",
          "[hash][windows][cng]")
{
    // The case the double free was in: an error path releases, and then the
    // destructor releases again on the way out of the scope.
    const int ordinal = GENERATE(0, 1, 2, 3, 4, 5);
    INFO("failing CNG call number " << ordinal);
    CngFailure fail(ordinal);

    {
        AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(QByteArrayLiteral("x"));
        (void)hash.result();
    }
    SUCCEED("destroyed after a refusal without corrupting the heap");
}

TEST_CASE("The injection really does refuse the call it names",
          "[hash][windows][cng]")
{
    // Guards the two cases above. Both accept an empty result OR a correct
    // one, because which calls have already happened decides what is
    // salvageable -- so neither would notice an injection that never fired.
    // Refusing the very first call cannot leave a usable hash behind.
    CngFailure fail(0);
    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("abc"));
    CHECK(hash.result().isEmpty());
    CHECK(AcceleratedCryptographicHash::cngCallCountForTest() > 0);
}

TEST_CASE("With nothing injected the hash is still correct",
          "[hash][windows][cng]")
{
    // The injection is off by default, and has to stay off: a seam that
    // leaked into an ordinary run would corrupt every hash the product takes.
    AcceleratedCryptographicHash::failNextCngCallForTest(-1);
    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("abc"));
    CHECK(hash.result()
          == QCryptographicHash::hash(QByteArrayLiteral("abc"),
                                      QCryptographicHash::Sha256));
}
#endif
