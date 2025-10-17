#ifndef WINDOWS_SUSPEND_INHIBITOR_H
#define WINDOWS_SUSPEND_INHIBITOR_H

#include "suspend_inhibitor.h"

#include <windows.h>

class WindowsSuspendInhibitor : public SuspendInhibitor
{
    EXECUTION_STATE _previousState;
public:
    WindowsSuspendInhibitor();
    virtual ~WindowsSuspendInhibitor();
};

#endif /* WINDOWS_SUSPEND_INHIBITOR_H */