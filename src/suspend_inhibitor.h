#ifndef SUSPEND_INHIBITOR_H
#define SUSPEND_INHIBITOR_H

class SuspendInhibitor : QObject
{
    QOBJECT
protected:
    SuspendInhibitor();
public:
    virtual ~SuspendInhibitor();
};

SuspendInhibitor *CreateSuspendInhibitor();

#endif /* SUSPEND_INHIBITOR_H */