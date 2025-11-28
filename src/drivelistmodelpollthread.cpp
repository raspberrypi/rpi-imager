#include "drivelistmodelpollthread.h"
#include <QElapsedTimer>
#include <QDebug>

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

DriveListModelPollThread::DriveListModelPollThread(QObject *parent)
    : QThread(parent), _terminate(false), _scanMode(ScanMode::Normal)
{
    qRegisterMetaType< std::vector<Drivelist::DeviceDescriptor> >( "std::vector<Drivelist::DeviceDescriptor>" );
}

DriveListModelPollThread::~DriveListModelPollThread()
{
    _terminate = true;
    _modeChanged.wakeAll();  // Wake thread if it's waiting
    if (!wait(2000)) {
        terminate();
    }
}

void DriveListModelPollThread::stop()
{
    _terminate = true;
    _modeChanged.wakeAll();  // Wake thread to check terminate flag
}

void DriveListModelPollThread::start()
{
    _terminate = false;
    QThread::start();
}

void DriveListModelPollThread::setScanMode(ScanMode mode)
{
    QMutexLocker lock(&_mutex);
    if (_scanMode != mode) {
        ScanMode oldMode = _scanMode;
        _scanMode = mode;
        
        const char* modeStr = (mode == ScanMode::Normal) ? "Normal" :
                              (mode == ScanMode::Slow) ? "Slow" : "Paused";
        qDebug() << "Drive scan mode changed to:" << modeStr;
        
        // Wake thread if transitioning from paused to active
        if (oldMode == ScanMode::Paused && mode != ScanMode::Paused) {
            _modeChanged.wakeAll();
        }
        
        emit scanModeChanged(mode);
    }
}

DriveListModelPollThread::ScanMode DriveListModelPollThread::scanMode() const
{
    QMutexLocker lock(&_mutex);
    return _scanMode;
}

void DriveListModelPollThread::pause()
{
    setScanMode(ScanMode::Paused);
}

void DriveListModelPollThread::resume()
{
    setScanMode(ScanMode::Normal);
}

void DriveListModelPollThread::run()
{
    QElapsedTimer t1;

    while (!_terminate)
    {
        // Check current scan mode
        ScanMode currentMode;
        {
            QMutexLocker lock(&_mutex);
            currentMode = _scanMode;
        }
        
        if (currentMode == ScanMode::Paused) {
            // Wait until mode changes or we're told to terminate
            QMutexLocker lock(&_mutex);
            while (_scanMode == ScanMode::Paused && !_terminate) {
                _modeChanged.wait(&_mutex, 500);  // Check every 500ms for terminate
            }
            continue;  // Re-check mode after waking
        }
        
        // Perform the scan
        t1.start();
        emit newDriveList( Drivelist::ListStorageDevices() );
        if (t1.elapsed() > 1000)
            qDebug() << "Enumerating drives took a long time:" << t1.elapsed()/1000.0 << "seconds";
        
        // Sleep based on current mode
        int sleepSeconds = (currentMode == ScanMode::Slow) ? 5 : 1;
        
        // Use interruptible sleep - check mode periodically
        for (int i = 0; i < sleepSeconds && !_terminate; ++i) {
            QMutexLocker lock(&_mutex);
            if (_scanMode == ScanMode::Paused) {
                break;  // Mode changed to paused, exit sleep early
            }
            lock.unlock();
            QThread::sleep(1);
        }
    }
}
