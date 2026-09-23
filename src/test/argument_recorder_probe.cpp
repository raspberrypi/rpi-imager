/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Records the arguments it was given, one per line, and exits.
 *
 * A real executable rather than a script, because a script cannot answer the
 * question these cases ask. The first attempt at this on Windows was a .cmd
 * file, and cmd.exe parses its own command line before the batch body ever
 * runs: a URL containing "&id=abc" arrived as two arguments, which looks
 * exactly like the bug under test and is not. An .exe receives its arguments
 * from the OS and reports what it was actually handed.
 *
 * The log path arrives in RPI_RECORDER_LOG rather than as an argument, so
 * everything on the command line is a value under test and nothing has to be
 * skipped when counting.
 *
 * Qt only on Windows, where it is what turns the UTF-16 command line into
 * arguments. Elsewhere Qt's arguments are argv as it stands, and this stands
 * in for xdg-open or runuser -- programs that must not need the application's
 * libraries, which is why launchDetached() strips LD_LIBRARY_PATH before
 * starting them. A recorder linked against that Qt could not start without
 * the very variable the code under test removes.
 */

#ifdef _WIN32

#include <QCoreApplication>
#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QByteArray logPath = qgetenv("RPI_RECORDER_LOG");
    if (logPath.isEmpty())
        return 2;

    QFile log(QString::fromLocal8Bit(logPath));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append))
        return 3;

    // app.arguments() rather than argv, so the arguments are decoded the same
    // way the application under test would decode them.
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        log.write(args.at(i).toUtf8());
        log.write("\n");
    }
    log.close();

    bool ok = false;
    const int wanted = qEnvironmentVariableIntValue("RPI_RECORDER_EXIT", &ok);
    return ok ? wanted : 0;
}

#else

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    const char *logPath = std::getenv("RPI_RECORDER_LOG");
    if (!logPath || !*logPath)
        return 2;

    std::FILE *log = std::fopen(logPath, "a");
    if (!log)
        return 3;

    for (int i = 1; i < argc; ++i) {
        std::fputs(argv[i], log);
        std::fputc('\n', log);
    }
    std::fclose(log);

    const char *wanted = std::getenv("RPI_RECORDER_EXIT");
    return wanted ? std::atoi(wanted) : 0;
}

#endif
