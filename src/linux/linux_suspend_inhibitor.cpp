#include "linux_suspend_inhibitor.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include <QtDBus/QtDBus>

GnomeSuspendInhibitor::GnomeSuspendInhibitor()
{
    _serviceFound = false;

    if (_bus.isConnected())
    {
        auto availableServices = _bus.interface()->registeredServiceNames().value();

        QString service = "org.gnome.SessionManager";
        QString path = "/org/gnome/SessionManager";

        if (availableServices.contains(service))
        {
            _serviceFound = true;

            QDBusInterface sessionManagerInterface(service, path, service, _bus);

            if (sessionManagerInterface.isValid())
            {
                uint32_t xid = 0;
                uint32_t flags = 0x4; // Inhibit suspending the session or computer

                QDBusReply<unsigned int> reply;

                reply = sessionManagerInterface.call("Inhibit", "Raspberry Pi Imager", xid, "Imaging", flags);

                _cookie = reply.value();
            }
        }
    }
}

GnomeSuspendInhibitor::~GnomeSuspendInhibitor()
{
    if (_bus.isConnected() && _serviceFound)
    {
        QDBusInterface sessionManagerInterface(service, path, service, _bus);

        if (sessionManagerInterface.isValid())
            sessionManagerInterface.call("Uninhibit", _cookie);
    }
}

ProcessScopedSuspendInhibitor::ProcessScopedSuspendInhibitor(const char *fileName, const char *arg)
{
    // Assumes:
    // - fileName refers to a tool that takes a command-line to run as a child process
    //   and can inhibit power management activity such as sleeping as long as that
    //   process runs.
    // - it can be configured with only one argument :-) if this ever needs to be
    //   expanded to supply more than one parameter to the inhibitor program, it will
    //   need to be reworked to use a full-on argument vector and execvp
    //
    // Action: Run fileName, and have it wrap: cat /tmp/fifo
    //
    // This inner command opens and reads from the supplied temporary FIFO.
    // We connect to the FIFO by opening it on our end, and we can then
    // terminate that read at any time by closing our end, which then
    // unblocks the inhibitor.

    // Generate and claim a unique filename for the FIFO.
    strcpy(_fifoName, "/tmp/suspend_fifo_XXXXXX");

    int fd = mkstemp(_fifoName);

    if (fd < 0)
    {
        // It just isn't working out. :-(
        _controlFd = -1;
        _fifoName[0] = '\0';
        return;
    }

    // The name was claimed with a regular file. Close the handle to that file
    // and then replace the file with a FIFO.
    close(fd);
    unlink(_fifoName);
    
    if (mkfifo(_fifoName, 0600) < 0)
    {
        // Can't make the FIFO. Oh well. :-(
        _controlFd = -1;
        _fifoName[0] = '\0';
        return;
    }

    // Open the FIFO, creating our end of the pipe.
    _controlFd = open(_fifoName, O_RDWR);

    if (_controlFd < 0)
    {
        _fifoName[0] = '\0';
        return;
    }

    // Fork in order to exec the inhibitor tool.
    int forkResult = fork();

    if (forkResult < 0)
    {
        // The call to fork failed, the whole enterprise is going under.
        CleanUp();
    }
    else if (forkResult == 0)
    {
        // If we get here, we're the child process. Close our copy of _controlFd.
        close(_controlFd);

        // Prepare an sh internal command that will block trying to read from the pipe.
        char waitCmd[100];

        snprintf(waitCmd, sizeof(waitCmd), "cat %s", _fifoName);

        // Run the inhibitor tool, and have it wrap an instance of sh that we can unblock
        // by closing the pipe. (execlp() does not return)
        execlp(
            fileName,
            fileName,
            arg,
            "sh",
            "-c",
            waitCmd,
            NULL);
    }
}

void ProcessScopedSuspendInhibitor::CleanUp()
{
    if (_controlFd)
        close(_controlFd);
    if (_fifoName[0])
        unlink(_fifoName);

    _controlFd = -1;
    _fifoName[0] = '\0';
}

ProcessScopedSuspendInhibitor::~ProcessScopedSuspendInhibitor()
{
    // Unblock the process that systemd-inhibit is waiting on.
    CleanUp();
}

LinuxSuspendInhibitor::LinuxSuspendInhibitor()
    : _kdeInhibitor("kde-inhibit", "--power"),
      _systemdInhibitor("systemd-inhibit", "--who=Raspberry Pi Imager")
{
}

/*virtual*/ LinuxSuspendInhibitor::~LinuxSuspendInhibitor() {}

SuspendInhibitor *CreateSuspendInhibitor()
{
    return new LinuxSuspendInhibitor();
}
