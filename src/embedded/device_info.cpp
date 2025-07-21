#include "device_info.h"
#include <QFile>
#include <QTextStream>
#include <QJsonObject>
#include <QDebug>

class DeviceInfo::impl{
    bool _is_raspberry_pi = false;
    QString _revision = "";
    QJsonArray _tags = {};
    bool _tags_set = false;
    QString _hardware_name = "";
   
public:
    void determineHardware()
    {
        // Attempt to open /proc/cpuinfo
        QFile f("/proc/cpuinfo");
        if (f.open(QFile::ReadOnly)) {
            QTextStream s(&f);
            QString line;
            do {
                line = s.readLine();            
                if (line.startsWith("Revision")) {
                    _revision = line.section(':', 1).trimmed();
                    bool ok;
                    uint32_t value_int = _revision.toUInt(&ok, 16);
                    if (!ok) { 
                        qDebug() << "Could not decipher revision code";
                        _hardware_name = {};
                        _is_raspberry_pi = false;
                    }
                    else {
                        // New Style Revision Code
                        // Device type is 8 bits, stored in bits 4-11
                        uint32_t extracted_value = (value_int >> 4) & 0xFF;

                        // device_code map
                        std::unordered_map<uint32_t, std::string> pi_revision_map = {
                            {0x00, "Raspberry Pi 1"},       // A
                            {0x01, "Raspberry Pi 1"},       // B
                            {0x02, "Raspberry Pi 1"},       // A+
                            {0x03, "Raspberry Pi 1"},       // B+
                            {0x04, "Raspberry Pi 2"},       // 2B
                            {0x06, "Raspberry Pi 1"},       // CM1
                            {0x08, "Raspberry Pi 3"},       // 3B
                            {0x09, "Raspberry Pi Zero"},    // Zero
                            {0x0a, "Raspberry Pi 3"},       // CM3
                            {0x0c, "Raspberry Pi Zero"},    // Zero W
                            {0x0d, "Raspberry Pi 3"},       // 3B+
                            {0x0e, "Raspberry Pi 3"},       // 3A+
                            {0x10, "Raspberry Pi 3"},       // CM3+
                            {0x11, "Raspberry Pi 4"},       // 4B
                            {0x12, "Raspberry Pi Zero 2 W"},// Zero 2 W
                            {0x13, "Raspberry Pi 4"},       // 400
                            {0x14, "Raspberry Pi 4"},       // CM4
                            {0x15, "Raspberry Pi 4"},       // CM4S
                            {0x17, "Raspberry Pi 5"},       // 5B
                            {0x18, "Raspberry Pi 5"},       // CM5
                            {0x19, "Raspberry Pi 5"},       // 500
                            {0x1a, "Raspberry Pi 5"}        // CM5 Lite
                        };
                        auto revision_str = pi_revision_map.at(extracted_value);
                        if (revision_str.length() > 0){
                            qDebug() << "Hardware Type Detected as: ";
                            _hardware_name = QString::fromStdString(revision_str);
                            qDebug() << _hardware_name;
                            _is_raspberry_pi = true;
                        }
                        else {
                            qDebug() << "Could not decipher revision code";
                            _hardware_name = {};
                            _is_raspberry_pi = false;
                        }
                    }
                }
            } while(!line.isNull());
        }
        else {
            qDebug() << "Failed to open /proc/cpuinfo";
            _hardware_name = {};
            _is_raspberry_pi = false;
        }
    }

    void setHardwareTags(QJsonArray deviceArray)
    {   
        // At this point, the OS List has been retrieved, a signal has been sent to run this section
        // Also a good time to determine whether or not this is a Raspberry Pi Device
        determineHardware();
        // Determine the correct filtering tags for use within the OSListFilter
        for (const QJsonValue &deviceValue: deviceArray) {
            QJsonObject deviceObj = deviceValue.toObject();
            if (deviceObj["name"].toString() == _hardware_name){
                _tags = deviceObj["tags"].toArray();
                _tags_set = true;
            }
        }
    }

    bool isRaspberryPi()
    {
        return _is_raspberry_pi;
    }

    bool hardwareTagsSet()
    {
        return _tags_set;
    }

    QString revision()
    {
        return _revision;
    }
    QString hardwareName()
    {
        return _hardware_name;
    }

    QJsonArray getHardwareTags()
    {
        return _tags;
    }
};

DeviceInfo::DeviceInfo() : p_Impl(std::make_unique<impl>()) {}
DeviceInfo::~DeviceInfo() = default;
bool DeviceInfo::isRaspberryPi() { return p_Impl->isRaspberryPi(); }
void DeviceInfo::determineHardware() { return p_Impl->determineHardware(); }
void DeviceInfo::setHardwareTags(QJsonArray deviceArray) { p_Impl->setHardwareTags(deviceArray); } // Corrected forwarding
bool DeviceInfo::hardwareTagsSet() { return p_Impl->hardwareTagsSet(); }
QString DeviceInfo::revision() { return p_Impl->revision();}
QString DeviceInfo::hardwareName() { return p_Impl->hardwareName(); }
QJsonArray DeviceInfo::getHardwareTags() { return p_Impl->getHardwareTags(); }