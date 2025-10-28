#ifndef SUSPEND_INHIBITOR_H
#define SUSPEND_INHIBITOR_H

#include <QObject>

class SuspendInhibitor : public QObject
{
    Q_OBJECT
protected:
    SuspendInhibitor();
public:
    virtual ~SuspendInhibitor();
};

SuspendInhibitor *CreateSuspendInhibitor();

#endif /* SUSPEND_INHIBITOR_H */