#include "windows_suspend_inhibitor.h"

#include <windows.h>

WindowsSuspendInhibitor::WindowsSuspendInhibitor()
{
    // Prevent both system suspend and display sleep
    _previousState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
}

/*virtual*/ WindowsSuspendInhibitor::~WindowsSuspendInhibitor()
{
    SetThreadExecutionState(_previousState);
}

SuspendInhibitor *CreateSuspendInhibitor()
{
    return new WindowsSuspendInhibitor();
}
