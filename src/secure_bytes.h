// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Bytes that are wiped before they are released, and never shared.
//
// A QByteArray cannot do this. It is implicitly shared, so the moment a
// secret is handed to a caller the two hold one buffer between them -- and
// the non-const data() that a wipe needs detaches: it allocates a fresh
// buffer, copies the secret into it, and zeroes the copy. The original is
// left exactly where it was, and the code reads as though it had been
// cleared.
//
// This owns its storage outright, hands out copies rather than references to
// it, and zeroes what it owns before letting it go.

#ifndef RPI_IMAGER_SECURE_BYTES_H
#define RPI_IMAGER_SECURE_BYTES_H

#include <QByteArray>

#include <cstddef>
#include <cstring>
#include <vector>

namespace rpi_imager {

// Zero `n` bytes at `p` in a way the compiler may not remove.
//
// A plain memset over storage about to be freed is dead by every rule the
// optimiser has, and gets deleted -- which is how a wipe becomes a comment.
// Written through a volatile pointer, so the writes have to happen.
inline void secureZero(void *p, std::size_t n)
{
    if (!p || n == 0)
        return;
    volatile unsigned char *q = static_cast<volatile unsigned char *>(p);
    while (n--)
        *q++ = 0;
}

class SecureBytes
{
public:
    SecureBytes() = default;
    ~SecureBytes() { wipe(); }

    // Neither copied nor assigned: every copy is another place the secret
    // lives, and this type exists to know where they all are.
    SecureBytes(const SecureBytes &) = delete;
    SecureBytes &operator=(const SecureBytes &) = delete;

    // Takes a copy of the bytes, so nothing is shared with the source. The
    // caller is expected to wipe its own copy if it had one.
    void assign(const char *data, std::size_t n)
    {
        wipe();
        if (!data || n == 0)
            return;
        _bytes.resize(n);
        std::memcpy(_bytes.data(), data, n);
    }

    void assign(const QByteArray &from)
    {
        assign(from.constData(), static_cast<std::size_t>(from.size()));
    }

    // A copy for the caller, which shares nothing with this object -- so
    // wiping here cannot reach into the caller's, and the caller's lifetime
    // cannot keep this one alive.
    QByteArray copy() const
    {
        if (_bytes.empty())
            return {};
        return QByteArray(_bytes.data(), static_cast<qsizetype>(_bytes.size()));
    }

    bool empty() const { return _bytes.empty(); }
    std::size_t size() const { return _bytes.size(); }

    // The storage itself, for a caller that must read it in place -- and for
    // the cases that check the wipe actually happened.
    const char *data() const { return _bytes.empty() ? nullptr : _bytes.data(); }

    // Zero what is held, then release it. Safe to call more than once.
    void wipe()
    {
        if (_bytes.empty())
            return;
        secureZero(_bytes.data(), _bytes.size());
        _bytes.clear();
        _bytes.shrink_to_fit();
    }

private:
    std::vector<char> _bytes;
};

} // namespace rpi_imager

#endif // RPI_IMAGER_SECURE_BYTES_H
