#ifndef MACFILE_H
#define MACFILE_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QByteArray>
#include <QFile>

namespace rpi_imager::mac {

// Run a helper that hands a descriptor back over its stdout socket, the way
// /usr/libexec/authopen does, feeding it stdinData. -1 if none arrived.
//
// Split out of MacFile::authOpen so it can be tested: the SCM_RIGHTS receive
// is the delicate part, and reaching it through authOpen means an
// authorisation prompt no unattended run can answer.
int openViaHelper(const char *program, const char *const argv[],
                  const QByteArray &stdinData);

}  // namespace rpi_imager::mac

class MacFile : public QFile
{
    Q_OBJECT
public:
    enum authOpenResult {authOpenCancelled, authOpenSuccess, authOpenError };

    MacFile(QObject *parent = nullptr);
    virtual bool isSequential() const;
    authOpenResult authOpen(const QByteArray &filename);
    bool forceSync();
};

#endif // MACFILE_H
