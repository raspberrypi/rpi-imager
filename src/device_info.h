#ifndef DEVICEINFO_H

#define DEVICEINFO_H
#include <QFile>
#include <QTextStream>
#include <QJsonArray>
#include <memory>

//
class DeviceInfo
{
    private:
        struct impl;
        std::unique_ptr<impl> p_Impl;
    public:
        DeviceInfo(); // Add parent parameter if it's a QObject
        ~DeviceInfo(); // Declare destructor
        bool isRaspberryPi();
        void determineHardware();
        bool hardwareTagsSet();
        QString revision();
        QString hardwareName();
        void setHardwareTags(QJsonArray deviceArray);
        QJsonArray getHardwareTags();

};

//
// class device_info
// {
// public:
//     device_info();
//     bool isRaspberryPi();
//     bool determineHardware();
//     bool hardwareTagsSet();
//     QString revision();
//     QString hardwareName();
//     bool setHardwareTags(QJsonArray deviceArray);
//     QJsonArray getHardwareTags();
// private:
//     bool _is_raspberry_pi = false;
//     QString _revision = "";
//     QString _device_code = "";
//     QJsonArray _tags = {};
//     bool _tags_set = false;
//     QString _hardware_name = "";
// };


#endif