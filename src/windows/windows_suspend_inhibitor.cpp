#include "windows_suspend_inhibitor.h"

#include <windows.h>

WindowsSuspendInhibitor::WindowsSuspendInhibitor()
{
    _previousState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
}

/*virtual*/ WindowsSuspendInhibitor::~WindowsSuspendInhibitor()
{
    SetThreadExecutionState(_previousState);
}

SuspendInhibitor *CreateSuspendInhibitor()
{
    return new WindowsSuspendInhibitor();
}
