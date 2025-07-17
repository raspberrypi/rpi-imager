#include "device_info.h"
#include <QFile>
#include <QTextStream>
#include <QJsonObject>
#include <QDebug>

struct DeviceInfo::impl {};

DeviceInfo::DeviceInfo() = default;
DeviceInfo::~DeviceInfo() = default;
bool DeviceInfo::isRaspberryPi() { return false; }
void DeviceInfo::determineHardware() { }
void DeviceInfo::setHardwareTags(QJsonArray) { }
bool DeviceInfo::hardwareTagsSet() { return false; }
QString DeviceInfo::revision() { return {};}
QString DeviceInfo::hardwareName() { return {}; }
QJsonArray DeviceInfo::getHardwareTags() { return {}; }