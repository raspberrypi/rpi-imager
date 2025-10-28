#include "linux_suspend_inhibitor.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include <QtDBus/QtDBus>

namespace {
    constexpr const char* service = "org.gnome.SessionManager";
    constexpr const char* path = "/org/gnome/SessionManager";
}

GnomeSuspendInhibitor::GnomeSuspendInhibitor()
{
    _serviceFound = false;

    if (_bus.isConnected())
    {
        auto availableServices = _bus.interface()->registeredServiceNames().value();

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
    : _controlFd(-1), _childPid(-1)
{
    // Assumes:
    // - fileName refers to a tool that takes a command-line to run as a child process
    //   and can inhibit power management activity such as sleeping as long as that
    //   process runs.
    // - it can be configured with only one argument :-) if this ever needs to be
    //   expanded to supply more than one parameter to the inhibitor program, it will
    //   need to be reworked to use a full-on argument vector and execvp
    //
    // Action: Run fileName, and have it wrap: cat /run/fifo
    //
    // This inner command opens and reads from the supplied temporary FIFO.
    // We connect to the FIFO by opening it on our end, and we can then
    // terminate that read at any time by closing our end, which then
    // unblocks the inhibitor.

    // Generate and claim a unique filename for the FIFO in /run (runtime directory).
    snprintf(_fifoName, sizeof(_fifoName), "/run/rpi-imager-suspend_XXXXXX");

    int fd = mkstemp(_fifoName);

    if (fd < 0)
    {
        // It just isn't working out. :-(
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
        _fifoName[0] = '\0';
        return;
    }

    // Open the FIFO, creating our end of the pipe.
    _controlFd = open(_fifoName, O_RDWR);

    if (_controlFd < 0)
    {
        // Failed to open, clean up the FIFO file
        unlink(_fifoName);
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
        // If we get here, we're the child process.
        
        // Close all file descriptors except stdin/stdout/stderr to prevent
        // the exec'd program from accessing disk devices or other resources.
        // The parent may have disk devices, network sockets, etc. open.
        long maxfd = sysconf(_SC_OPEN_MAX);
        if (maxfd == -1)
            maxfd = 1024; // Fallback if sysconf fails
        
        for (int fd = 3; fd < maxfd; fd++)
        {
            close(fd); // Ignore errors - fd may not be open
        }

        // Drop privileges before exec'ing external programs.
        // The imager runs with elevated privileges to write to disks, but the
        // inhibitor tools don't need those privileges.
        if (getuid() != geteuid())
        {
            // Drop effective privileges back to real user
            if (setgid(getgid()) != 0 || setuid(getuid()) != 0)
            {
                // Failed to drop privileges - abort for safety
                _exit(126);
            }
        }

        // Run the inhibitor tool, and have it wrap cat reading from the FIFO.
        // We avoid using shell to prevent any potential command injection issues.
        // (execlp() does not return on success)
        execlp(
            fileName,
            fileName,
            arg,
            "cat",
            _fifoName,
            NULL);
        
        // If we get here, exec failed. Must use _exit() not exit() in forked child.
        _exit(127);
    }
    else
    {
        // Parent process: store the child PID so we can clean it up later
        _childPid = forkResult;
    }
}

void ProcessScopedSuspendInhibitor::CleanUp()
{
    // Close the FIFO, which will unblock the child process
    if (_controlFd >= 0)
    {
        close(_controlFd);
        _controlFd = -1;
    }
    
    // Wait for the child process to exit (prevents zombie processes)
    if (_childPid > 0)
    {
        int status;
        waitpid(_childPid, &status, 0);
        _childPid = -1;
    }
    
    // Remove the FIFO file
    if (_fifoName[0])
    {
        unlink(_fifoName);
        _fifoName[0] = '\0';
    }
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
