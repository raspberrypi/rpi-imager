#include "device_info.h"
#include "revision_code.h"
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
                    bool parsed = false;
                    _hardware_name = rpi_imager::hardwareNameForRevision(_revision, &parsed);
                    _is_raspberry_pi = !_hardware_name.isEmpty();
                    if (!parsed)
                        qDebug() << "Could not decipher revision code";
                    else if (_is_raspberry_pi)
                        qDebug() << "Hardware Type Detected as:" << _hardware_name;
                    else
                        qDebug() << "Could not decipher revision code" << _revision
                                 << "(device type" << Qt::hex
                                 << rpi_imager::deviceTypeOf(_revision.toUInt(nullptr, 16))
                                 << Qt::dec << ")";
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