#include "freebsd_suspend_inhibitor.h"
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
#include <limits.h>
#include <errno.h>
#include <string>

#include <QtDBus/QtDBus>

namespace {
    constexpr const char* service = "org.gnome.SessionManager";
    constexpr const char* path = "/org/gnome/SessionManager";
}

GnomeSuspendInhibitor::GnomeSuspendInhibitor()
{
}

GnomeSuspendInhibitor::~GnomeSuspendInhibitor()
{
}

ProcessScopedSuspendInhibitor::ProcessScopedSuspendInhibitor(const char *fileName, std::vector<std::string> args)
    : _controlFd(-1), _childPid(-1)
{
}

void ProcessScopedSuspendInhibitor::CleanUp()
{
}

ProcessScopedSuspendInhibitor::~ProcessScopedSuspendInhibitor()
{
}

FreeBSDSuspendInhibitor::FreeBSDSuspendInhibitor()
    : _kdeInhibitor("kde-inhibit", {"--power", "--screen"}),
      _systemdInhibitor("systemd-inhibit", {"--what=idle:sleep", "--who=Raspberry Pi Imager"})
{
}

/*virtual*/ FreeBSDSuspendInhibitor::~FreeBSDSuspendInhibitor() {}

SuspendInhibitor *CreateSuspendInhibitor()
{
    return nullptr;
}
