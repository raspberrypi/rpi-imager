/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 *
 * Linux drive enumeration using lsblk.
 *
 * Design notes:
 * - Uses lsblk with JSON output for reliable parsing
 * - Parsing logic is separated from command execution for testability
 * - Handles various edge cases: loop devices, NVMe, SD cards, internal readers
 */

#include "drivelist.h"
#include "embedded_config.h"

#include <optional>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace Drivelist {

namespace {

// Maximum recursion depth for walking device children
// Prevents stack overflow from malformed or malicious lsblk output
constexpr int MAX_CHILD_RECURSION_DEPTH = 10;

/**
 * @brief Walk device children to collect mountpoints and labels
 * @param depth Current recursion depth (starts at 0)
 */
void walkStorageChildren(DeviceDescriptor& device, QStringList& labels, const QJsonArray& children, int depth = 0)
{
    if (depth >= MAX_CHILD_RECURSION_DEPTH) {
        qWarning() << "walkStorageChildren: max recursion depth reached, stopping traversal";
        return;
    }
    
    for (const auto& child : children) {
        QJsonObject childObj = child.toObject();
        QString label = childObj["label"].toString();
        QString mountpoint = childObj["mountpoint"].toString();

        if (!label.isEmpty()) {
            labels.append(label);
        }
        if (!mountpoint.isEmpty()) {
            device.mountpoints.push_back(mountpoint.toStdString());
            device.mountpointLabels.push_back(label.toStdString());
        }

        // Recurse into nested children (e.g., LVM on partition)
        QJsonArray subChildren = childObj["children"].toArray();
        if (!subChildren.isEmpty()) {
            walkStorageChildren(device, labels, subChildren, depth + 1);
        }
    }
}

/**
 * @brief Parse a single block device from JSON
 */
std::optional<DeviceDescriptor> parseBlockDevice(const QJsonObject& bdev, bool embeddedMode)
{
    DeviceDescriptor device;

    QString name = bdev["kname"].toString();
    QString subsystems = bdev["subsystems"].toString();

    // Skip devices we never want to show
    if (name.isEmpty()) return std::nullopt;

    // Skip CD/DVD drives, RAM devices, compressed RAM
    if (name.startsWith("/dev/sr") ||
        name.startsWith("/dev/ram") ||
        name.startsWith("/dev/zram")) {
        return std::nullopt;
    }

    // Skip eMMC boot partitions (special hardware boot areas)
    if (name.contains("boot") && name.contains("mmcblk")) {
        return std::nullopt;
    }

    // Populate basic device info
    device.device = name.toStdString();
    device.raw = name.toStdString();  // Linux uses same path for raw access
    device.busType = bdev["busType"].toString().toStdString();

    // Determine if virtual based on subsystems
    // Pure block devices (subsystems == "block") are virtual (loop devices, etc.)
    // Physical devices have additional subsystems like "block:scsi:usb:pci"
    //
    // Loop devices are always virtual by definition — they are kernel constructs
    // backed by files. We check the device name explicitly because lsblk in
    // util-linux 2.39.x (shipped in Ubuntu 24.04 LTS) has a bug where the
    // "subsystems" column intermittently returns empty for loop devices, causing
    // them to flicker between virtual/non-virtual.
    // See: https://github.com/util-linux/util-linux/pull/3089
    device.isVirtual = name.startsWith("/dev/loop") || (subsystems == "block");

    // Physical USB drives and MMC cards should never be marked as virtual
    if (subsystems.contains("usb") || subsystems.contains("mmc")) {
        device.isVirtual = false;
    }

    // Parse read-only and removable flags (lsblk returns bool or "0"/"1" string)
    if (bdev["ro"].isBool()) {
        device.isReadOnly = bdev["ro"].toBool();
        device.isRemovable = bdev["rm"].toBool() || bdev["hotplug"].toBool();
    } else {
        device.isReadOnly = (bdev["ro"].toString() == "1");
        device.isRemovable = (bdev["rm"].toString() == "1") || (bdev["hotplug"].toString() == "1");
    }

    // Parse size (can be string or number depending on lsblk version)
    if (bdev["size"].isString()) {
        device.size = bdev["size"].toString().toULongLong();
    } else {
        device.size = static_cast<uint64_t>(bdev["size"].toDouble());
    }

    // Detect connection type from subsystems
    device.isCard = subsystems.contains("mmc");
    device.isUSB = subsystems.contains("usb");
    device.isSCSI = subsystems.contains("scsi") && !device.isUSB;

    // SD cards in internal readers report rm=0 (reader is fixed), but the
    // media is removable. USB devices are always removable.
    if (device.isCard || device.isUSB) {
        device.isRemovable = true;
    }

    // System drive: non-removable and non-virtual
    device.isSystem = !device.isRemovable && !device.isVirtual;

    // Parse block sizes
    device.blockSize = static_cast<uint32_t>(bdev["phy-sec"].toInt());
    device.logicalBlockSize = static_cast<uint32_t>(bdev["log-sec"].toInt());
    if (device.blockSize == 0) device.blockSize = 512;
    if (device.logicalBlockSize == 0) device.logicalBlockSize = 512;

    // Build description from label, vendor, model
    // Pre-filter empty strings to avoid multiple removeAll("") calls
    QStringList descParts;
    descParts.reserve(4);
    
    auto addIfNotEmpty = [&descParts](const QString& s) {
        QString trimmed = s.trimmed();
        if (!trimmed.isEmpty()) {
            descParts.append(trimmed);
        }
    };
    
    addIfNotEmpty(bdev["label"].toString());
    addIfNotEmpty(bdev["vendor"].toString());
    addIfNotEmpty(bdev["model"].toString());

    // Special case for internal SD card reader
    if (name == "/dev/mmcblk0" && descParts.isEmpty()) {
        descParts.append(QObject::tr("Internal SD card reader"));
    }

    // Fallback for loop devices with no label/vendor/model
    if (name.startsWith("/dev/loop") && descParts.isEmpty()) {
        QString shortName = name.mid(5);  // "/dev/loop0" -> "loop0"
        descParts.append(QObject::tr("Loopback device (%1)").arg(shortName));
    }

    // Collect mountpoints from this device
    QString mountpoint = bdev["mountpoint"].toString();
    if (!mountpoint.isEmpty()) {
        device.mountpoints.push_back(mountpoint.toStdString());
        device.mountpointLabels.push_back(bdev["label"].toString().toStdString());
    }

    // Walk children (partitions) for additional mountpoints
    QStringList labels;
    QJsonArray children = bdev["children"].toArray();
    walkStorageChildren(device, labels, children);

    // Append partition labels to description
    if (!labels.isEmpty()) {
        descParts.append("(" + labels.join(", ") + ")");
    }
    
    // Sanitize description to prevent Unicode display attacks
    // (e.g., RTL override characters that could make device names misleading)
    device.description = sanitizeForDisplay(descParts.join(" ").toStdString());

    // Anything carrying a system mountpoint is a system drive, whether or
    // not it is removable.
    if (!device.isSystem && device.hasSystemMountpoint()) {
        device.isSystem = true;
    }

    // Handle NVMe drives: mark as system by default to avoid showing internal drives
    // In embedded mode (Raspberry Pi), allow unmounted NVMe drives
    if (device.isSystem && subsystems.contains("nvme")) {
        if (embeddedMode) {
            bool hasCriticalMount = false;
            for (const auto& mp : device.mountpoints) {
                if (!QString::fromStdString(mp).startsWith("/media/")) {
                    hasCriticalMount = true;
                    break;
                }
            }
            if (!hasCriticalMount) {
                device.isSystem = false;
            }
        }
        // Non-embedded mode: keep NVMe marked as system (filtered out)
    }

    return device;
}

/**
 * @brief Execute lsblk and return JSON output
 */
std::optional<QByteArray> executeLsblk()
{
    QProcess process;
    // Note: We intentionally do NOT exclude loop devices (major 7) because
    // mounted disk images are valid imaging targets. The isVirtual/isSystem
    // flags handle filtering appropriately.
    QStringList args = {
        "--bytes",
        "--json",
        "--paths",
        "--tree",
        "--output", "kname,type,subsystems,ro,rm,hotplug,size,phy-sec,log-sec,label,vendor,model,mountpoint"
    };

    process.start("lsblk", args);
    // Use 5 second timeout - embedded systems with many block devices or slow
    // USB hubs may take longer than 2 seconds to enumerate
    if (!process.waitForFinished(5000)) {
        qWarning() << "lsblk timed out after 5000ms";
        qWarning() << "  state:" << process.state() << "error:" << process.error();
        // Capture any partial stderr for debugging
        QByteArray partialStderr = process.readAllStandardError();
        if (!partialStderr.isEmpty()) {
            qWarning() << "  stderr:" << partialStderr;
        }
        process.kill();
        process.waitForFinished(1000);
        return std::nullopt;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning() << "lsblk failed with exit code" << process.exitCode();
        qWarning() << "  stderr:" << process.readAllStandardError();
        return std::nullopt;
    }

    QByteArray output = process.readAll();
    if (output.isEmpty()) {
        qWarning() << "lsblk returned empty output";
        return std::nullopt;
    }

    return output;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

// Everything ListStorageDevices() does once lsblk has answered -- or failed
// to. Separate from the call itself so the test API can drive the same code
// the application runs, including the two failure shapes: nullopt for an
// lsblk that could not be run, and unparseable output for one that answered
// with rubbish. Before this the test API had a second copy of the parsing
// that returned an empty list where this one returns the sentinel, so the
// behaviour a user actually meets was the one nothing checked.
std::vector<DeviceDescriptor> devicesFromLsblkOutput(
    const std::optional<QByteArray>& jsonOutput, bool embeddedMode)
{
    std::vector<DeviceDescriptor> deviceList;

    if (!jsonOutput) {
        // Return a sentinel device with error message so UI can display failure
        // instead of showing an empty list (which looks like "no drives found")
        DeviceDescriptor errorDevice;
        errorDevice.device = "__error__";
        errorDevice.error = "Failed to enumerate drives: lsblk command failed or timed out";
        errorDevice.description = "Drive enumeration failed";
        deviceList.push_back(std::move(errorDevice));
        return deviceList;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(*jsonOutput, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse lsblk JSON:" << parseError.errorString();
        DeviceDescriptor errorDevice;
        errorDevice.device = "__error__";
        errorDevice.error = "Failed to enumerate drives: " + parseError.errorString().toStdString();
        errorDevice.description = "Drive enumeration failed";
        deviceList.push_back(std::move(errorDevice));
        return deviceList;
    }

    const QJsonArray blockDevices = doc.object().value("blockdevices").toArray();

    // Reserve capacity to avoid reallocations during enumeration
    deviceList.reserve(static_cast<size_t>(blockDevices.size()));

    for (const auto& item : blockDevices) {
        auto device = parseBlockDevice(item.toObject(), embeddedMode);
        if (device) {
            deviceList.push_back(std::move(*device));
        }
    }

    return deviceList;
}

std::vector<DeviceDescriptor> ListStorageDevices()
{
    return devicesFromLsblkOutput(executeLsblk(), ::isEmbeddedMode());
}

// ============================================================================
// Test API
// ============================================================================

#ifdef DRIVELIST_ENABLE_TEST_API

namespace testing {

std::vector<DeviceDescriptor> parseLinuxBlockDevices(const std::string& jsonOutput, bool embeddedMode)
{
    return devicesFromLsblkOutput(QByteArray::fromStdString(jsonOutput), embeddedMode);
}

std::vector<DeviceDescriptor> devicesWhenLsblkCannotBeRun(bool embeddedMode)
{
    return devicesFromLsblkOutput(std::nullopt, embeddedMode);
}

// lsblk itself, run for real against whatever PATH the caller has arranged.
//
// The bounded wait inside it is the interesting part. Enumerating block
// devices goes out to the kernel and, through it, to whatever is plugged in;
// a stuck USB bridge or a slow hub can wedge lsblk for as long as it likes.
// The drive list is refreshed on a timer, so a wait without a limit does not
// just delay the list -- it takes the window with it.
std::optional<QByteArray> runLsblk()
{
    return executeLsblk();
}

} // namespace testing

#endif // DRIVELIST_ENABLE_TEST_API

} // namespace Drivelist
