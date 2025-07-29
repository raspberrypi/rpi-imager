#ifndef DEVICEINFO_H

#define DEVICEINFO_H
#include <QFile>
#include <QTextStream>
#include <QJsonArray>
#include <memory>


class DeviceInfo
{
    private:
        struct impl;
        std::unique_ptr<impl> p_Impl;
    public:
        DeviceInfo();
        ~DeviceInfo();
        bool isRaspberryPi();
        void determineHardware();
        bool hardwareTagsSet();
        QString revision();
        QString hardwareName();
        void setHardwareTags(QJsonArray deviceArray);
        QJsonArray getHardwareTags();
};

#endif