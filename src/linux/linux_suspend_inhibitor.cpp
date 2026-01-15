#include "linux_suspend_inhibitor.h"
#include "../platformquirks.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <pwd.h>

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
                // Inhibit both suspending and idle (which prevents display sleep)
                // 0x4 = Inhibit suspending the session or computer
                // 0x8 = Inhibit the session being marked as idle
                uint32_t flags = 0x4 | 0x8;

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

ProcessScopedSuspendInhibitor::ProcessScopedSuspendInhibitor(const char *fileName, std::vector<std::string> args)
    : _controlFd(-1), _childPid(-1)
{
    // Assumes:
    // - fileName refers to a tool that takes a command-line to run as a child process
    //   and can inhibit power management activity such as sleeping as long as that
    //   process runs.
    //
    // Action: Run fileName with args, and have it wrap: cat /run/fifo
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
        
        // Determine target UID and GID to drop to
        uid_t targetUid = 0;
        gid_t targetGid = 0;
        bool shouldDropPrivileges = false;
        
        if (getuid() != geteuid())
        {
            // Running under sudo: real UID is the original user
            targetUid = getuid();
            targetGid = getgid();
            shouldDropPrivileges = true;
        }
        else if (geteuid() == 0)
        {
            // Running as root - check if we were invoked via pkexec
            const char* pkexecUid = getenv("PKEXEC_UID");
            if (pkexecUid)
            {
                // Running under pkexec: need to look up the original user's GID
                targetUid = static_cast<uid_t>(atoi(pkexecUid));
                struct passwd* pw = getpwuid(targetUid);
                if (pw)
                {
                    targetGid = pw->pw_gid;
                    shouldDropPrivileges = true;
                }
            }
        }
        
        if (shouldDropPrivileges)
        {
            // Drop effective privileges back to the original user
            if (setgid(targetGid) != 0 || setuid(targetUid) != 0)
            {
                // Failed to drop privileges - abort for safety
                _exit(126);
            }
        }

        // Clear AppImage environment before running external tools
        PlatformQuirks::clearAppImageEnvironment();

        // Run the inhibitor tool, and have it wrap cat reading from the FIFO.
        // We avoid using shell to prevent any potential command injection issues.
        // Build argument vector: [fileName, ...args, "cat", _fifoName, NULL]
        std::vector<const char*> argv;
        argv.push_back(fileName);
        for (const auto& arg : args)
        {
            argv.push_back(arg.c_str());
        }
        argv.push_back("cat");
        argv.push_back(_fifoName);
        argv.push_back(NULL);
        
        // (execvp() does not return on success)
        execvp(fileName, const_cast<char* const*>(argv.data()));
        
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
    : _kdeInhibitor("kde-inhibit", {"--power", "--screen"}),
      _systemdInhibitor("systemd-inhibit", {"--what=idle:sleep", "--who=Raspberry Pi Imager"})
{
}

/*virtual*/ LinuxSuspendInhibitor::~LinuxSuspendInhibitor() {}

SuspendInhibitor *CreateSuspendInhibitor()
{
    return new LinuxSuspendInhibitor();
}
