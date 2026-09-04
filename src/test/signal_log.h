/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * A minimal stand-in for QSignalSpy.
 *
 * The Qt builds this project ships (/opt/Qt/6.11.1, all three variants) are
 * configured without the Qt Test module -- no libQt6Test, no headers. Linking
 * Qt6::Test therefore made the suite unbuildable against exactly the Qt that
 * goes out of the door, while passing happily against a distro Qt that does
 * include it. That is the wrong way round for a test suite to fail.
 *
 * Only two things were ever wanted from QSignalSpy: how many times a signal
 * fired, and the arguments it carried. Those are here.
 *
 * Like QSignalSpy, this connects with itself as the context object, so a
 * signal emitted on a worker thread is queued to the thread that owns the log
 * and delivered when that thread next runs its event loop -- which is what
 * the waitFor() helpers in these tests spin. Appends therefore all happen on
 * one thread.
 */

#ifndef RPI_TEST_SIGNAL_LOG_H
#define RPI_TEST_SIGNAL_LOG_H

#include <QList>
#include <QObject>
#include <QVariant>
#include <QVariantList>

namespace rpi_test {

class SignalLog : public QObject
{
public:
    template <typename Sender, typename Ret, typename... Args>
    SignalLog(Sender *sender, Ret (Sender::*signal)(Args...))
    {
        QObject::connect(sender, signal, this, [this](Args... args) {
            _rows.append(QVariantList{QVariant::fromValue(args)...});
        });
    }

    int count() const { return int(_rows.size()); }
    bool isEmpty() const { return _rows.isEmpty(); }
    const QVariantList &at(int i) const { return _rows.at(i); }

private:
    QList<QVariantList> _rows;
};

} // namespace rpi_test

#endif // RPI_TEST_SIGNAL_LOG_H
