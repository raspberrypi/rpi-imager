#ifndef FREEBSD_SUSPEND_INHIBITOR_H
#define FREEBSD_SUSPEND_INHIBITOR_H

#include "suspend_inhibitor.h"

#include <QtDBus/QtDBus>
#include <vector>
#include <string>

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
    ProcessScopedSuspendInhibitor(const char *fileName, std::vector<std::string> args);
    ~ProcessScopedSuspendInhibitor();
};

class FreeBSDSuspendInhibitor : public SuspendInhibitor
{
    GnomeSuspendInhibitor _gnomeInhibitor;
    ProcessScopedSuspendInhibitor _kdeInhibitor;
    ProcessScopedSuspendInhibitor _systemdInhibitor;
public:
    FreeBSDSuspendInhibitor();
    virtual ~FreeBSDSuspendInhibitor();
};

#endif /* FREEBSD_SUSPEND_INHIBITOR_H */
