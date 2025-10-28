#ifndef LINUX_SUSPEND_INHIBITOR_H
#define LINUX_SUSPEND_INHIBITOR_H

#include "suspend_inhibitor.h"

#include <QtDBus/QtDBus>

class GnomeSuspendInhibitor
{
    QDBusConnection _bus = QDBusConnection::sessionBus();
    bool _serviceFound;
    int _cookie;
public:
    GnomeSuspendInhibitor();
    ~GnomeSuspendInhibitor();
};

class ProcessScopedSuspendInhibitor
{
    char _fifoName[50];
    int _controlFd;
    pid_t _childPid;

    void CleanUp();
public:
    ProcessScopedSuspendInhibitor(const char *fileName, const char *arg);
    ~ProcessScopedSuspendInhibitor();
};

class LinuxSuspendInhibitor : public SuspendInhibitor
{
    GnomeSuspendInhibitor _gnomeInhibitor;
    ProcessScopedSuspendInhibitor _kdeInhibitor;
    ProcessScopedSuspendInhibitor _systemdInhibitor;
public:
    LinuxSuspendInhibitor();
    virtual ~LinuxSuspendInhibitor();
};

#endif /* LINUX_SUSPEND_INHIBITOR_H */
