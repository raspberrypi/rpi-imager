#ifndef MAC_SUSPEND_INHIBITOR_H
#define MAC_SUSPEND_INHIBITOR_H

#include "suspend_inhibitor.h"

#include <IOKit/pwr_mgt/IOPMLib.h>

class MacSuspendInhibitor : public SuspendInhibitor
{
    IOPMAssertionID _systemSleepAssertion;
    IOPMAssertionID _displaySleepAssertion;
public:
    MacSuspendInhibitor();
    virtual ~MacSuspendInhibitor();
};

#endif /* MAC_SUSPEND_INHIBITOR_H */
