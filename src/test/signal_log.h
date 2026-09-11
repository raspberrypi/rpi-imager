/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
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
    // Sender and Owner are deduced separately so a signal declared on a base
    // class can be logged from a pointer to a subclass -- which is what a test
    // that subclasses the thing under test to shorten its timings has.
    template <typename Sender, typename Owner, typename Ret, typename... Args>
    SignalLog(Sender *sender, Ret (Owner::*signal)(Args...))
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
