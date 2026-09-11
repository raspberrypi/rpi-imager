#ifndef LINUX_SUSPEND_INHIBITOR_H
#define LINUX_SUSPEND_INHIBITOR_H

#include "suspend_inhibitor.h"

#include <QtDBus/QtDBus>
#include <vector>
#include <string>

class GnomeSuspendInhibitor
{
    QDBusConnection _bus = QDBusConnection::sessionBus();
    bool _serviceFound;
    // Unsigned, and matching what Inhibit hands back: org.gnome.SessionManager
    // declares both the cookie it returns and the one Uninhibit takes as a
    // D-Bus 'u'. Sent as an 'i' the release call matches no method at all, and
    // the inhibit stays held until the process exits and the bus connection
    // takes it away -- so the machine will not idle or suspend for as long as
    // the application is left open after a write.
    //
    // Zero because the destructor can be reached without Inhibit having been
    // called: the service is on the bus but the interface does not come up.
    uint32_t _cookie = 0;
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
