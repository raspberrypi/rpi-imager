#include "mac_suspend_inhibitor.h"

#include <IOKit/pwr_mgt/IOPMLib.h>

MacSuspendInhibitor::MacSuspendInhibitor()
{
    CFStringRef name = CFSTR("Raspberry Pi Imager");

    auto result = IOPMAssertionCreateWithName(
        kIOPMAssertionTypePreventUserIdleSystemSleep,
        kIOPMAssertionLevelOn,
        name,
        &_powerAssertion);
        
    if (result != kIOReturnSuccess)
        _powerAssertion = kIOPMNullAssertionID;
}

/*virtual*/ MacSuspendInhibitor::~MacSuspendInhibitor()
{
    if (_powerAssertion != kIOPMNullAssertionID)
    {
        IOPMAssertionRelease(_powerAssertion);
        _powerAssertion = kIOPMNullAssertionID;
    }
}

SuspendInhibitor *CreateSuspendInhibitor()
{
    return new MacSuspendInhibitor();
}
