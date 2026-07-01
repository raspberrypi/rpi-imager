/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 *
 * FreeBSD drive enumeration.
 *
 * Design notes:
 * - Uses sysctl, libCAM, and libGEOM to enumerate disks and query properties
 * - Individual functions are provided for determing specific drive properties
 * - Handles some edge cases: Loop devices, NVMe, SD/MMC, ram disks
 */

#include "drivelist.h"
#include "embedded_config.h"

#include <fcntl.h>
#include <sys/mount.h>
#include <camlib.h>
#include <libgeom.h>
#include <sys/sysctl.h>
#include <algorithm>
#include <array>
#include <unordered_set>
#include <iostream>
#include <vector>
#include <optional>
#include <cassert>
#include <QString>
#include <QDebug>
#include <QStringList>

namespace Drivelist {

namespace {

/**
 * @brief Return the label associated with a GEOM provider, if it exists.
 */
std::optional<QString> getLabel(const struct gprovider *partition) {
    assert(QString(partition->lg_geom->lg_class->lg_name) == "PART");

    struct gconsumer *consumer;
    struct ggeom *label;
    struct gprovider *provider;

    LIST_FOREACH(consumer, &partition->lg_consumers, lg_consumer) {
        label = consumer->lg_geom;
        if (QString(label->lg_class->lg_name) == "LABEL") {
            provider = LIST_FIRST(&label->lg_provider);
            return provider->lg_name;
        }
    }

    return std::nullopt;
}

/**
 * @brief Return the label associated with a GEOM disk, if it exists.
 */
std::optional<QString> getLabel(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");
    struct gprovider *provider;

    LIST_FOREACH(provider, &disk->lg_provider, lg_provider) {
        if (QString(provider->lg_geom->lg_class->lg_name) != "PART") {
            continue;
        }
        auto label = getLabel(provider);
        if (label) {
            return *label;
        }
    }

    return std::nullopt;
}

/**
 * @brief Return the GEOM object representing a disk's partition table, if it exists.
 */
std::optional<const struct ggeom*> getDiskPartitionTable(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");

    struct gprovider *provider;
    struct gconsumer *consumer;

    // Get providers from a disk for a partition table
    LIST_FOREACH(provider, &disk->lg_provider, lg_provider) {
        // Check if any providers are consumed by a partition table
        LIST_FOREACH(consumer, &provider->lg_consumers, lg_consumer) {
            if (QString(consumer->lg_geom->lg_class->lg_name) == "PART") {
                return consumer->lg_geom;
            }
        }
    }

    return std::nullopt;
}

/**
 * @brief Return a list of GEOM objects representing the partitions in a table.
 */
std::vector<const struct gprovider*> getPartitionObjs(const struct ggeom *table) {
    assert(QString(table->lg_class->lg_name) == "PART");

    std::vector<const struct gprovider*> result;
    struct gprovider *partition;

    // Enumerate geom objects representing partitions in the table
    LIST_FOREACH(partition, &table->lg_provider, lg_provider) {
        result.push_back(partition);
    }

    return result;
}

/**
 * @brief Return a list of partitions mounted on a disk and its partitions.
 */
QStringList getMountpointList(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");

    QStringList mountpoints;
    const auto partitionTable = getDiskPartitionTable(disk);
    if (!partitionTable) {
        return mountpoints;
    }

    std::vector<const struct gprovider*> partitionObjs = getPartitionObjs(*partitionTable);
    std::unordered_set<QString> partitions;

    for (const auto& obj : partitionObjs) {
        partitions.insert(QString(g_device_path(obj->lg_name)));
    }

    struct statfs *mntlist;
    int nmnt = getmntinfo(&mntlist, MNT_WAIT);

    // Find mountpoints mounted on our partitions
    for (int i = 0; i < nmnt; i++) {
        QString from = mntlist[i].f_mntfromname;
        if (partitions.contains(from)) {
            QString on = mntlist[i].f_mntonname;
            mountpoints.append(on);
        }
    }

    return mountpoints;
}

/**
 * @brief Return a list of GEOM labels associated with a disk and its partitions.
 */
QStringList getMountpointLabelList(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");

    QStringList labels;
    const auto partitionTable = getDiskPartitionTable(disk);
    if (!partitionTable) {
        return labels;
    }

    std::vector<const struct gprovider*> partitionObjs = getPartitionObjs(*partitionTable);

    for (const auto& obj : partitionObjs) {
        auto label = getLabel(obj);
        if (label) {
            labels.append(*label);
        }
    }

    return labels;
}

/**
 * @brief Return the value associated with some key in the config of a GEOM.
 */
template <typename T>
std::optional<QString> getGeomConfig(const T *obj, const QString& key) {
    struct gconfig *confItem;

    // Search geom object config for a field matching item
    LIST_FOREACH(confItem, &obj->lg_config, lg_config) {
        if (QString(confItem->lg_name) == key) {
            return QString(confItem->lg_val);
        }
    }

    return std::nullopt;
}

/**
 * @brief Query CAM to determine if a GEOM disk is removable.
 */
bool isRemovable(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");

    const char *name = disk->lg_name;
    bool removable;

    struct cam_device *dev = cam_open_device(name, O_RDWR);
    if (dev == nullptr) {
        return false;
    }
    removable = SID_IS_REMOVABLE(&dev->inq_data);
    cam_close_device(dev);

    return removable;
}

/**
 * @brief Query sysctl to determine if a GEOM disk is write-protected.
 */
bool isReadOnly(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");

    const char *name = disk->lg_name;
    const QString oid = QString::asprintf("kern.geom.disk.%s.flags", name);

    char oldp[1024];
    size_t len = 1024;
    if (sysctlbyname(oid.toStdString().c_str(), oldp, &len, nullptr, 0) == -1) {
        return false;
    }

    // The string is of form be<flag1,flag2,...>. We wish to remove "^be<' and
    // ">$". Then, we may iterate over its tokens.
    QString rawFlagList = QString(oldp);
    rawFlagList.slice(3, rawFlagList.length() - 4);

    QStringList flags = rawFlagList.split(u',');

    return std::any_of(flags.cbegin(), flags.cend(),
                       [](const QString& i) { return i == "WRITEPROTECT"; });
}

/**
 * @brief Determine if a GEOM disk represents a ramdisk rather than a physical drive.
 */
bool isVirtual(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");
    return QString(disk->lg_class->lg_name) == "MD";
}

/**
 * @brief Determine if a system folder is mounted on a given GEOM disk.
 */
bool isBackingSystemPath(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");
    std::unordered_set<QString> systemPaths = {"/", "/usr", "/var", "/home", "/boot"};
    for (const auto& mountpoint : getMountpointList(disk)) {
        if (systemPaths.contains(mountpoint)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Lookup through CAM the interface a disk uses.
 */
cam_xport getTransportType(const struct ggeom *disk) {
    assert(QString(disk->lg_class->lg_name) == "DISK" ||
           QString(disk->lg_class->lg_name) == "MD");
    cam_xport transport = XPORT_UNKNOWN;
    union ccb *ccb = nullptr;
    struct cam_device *device = cam_open_device(disk->lg_name, O_RDWR);
    if (device == nullptr) {
        goto fail;
    }
    ccb = cam_getccb(device);
    if (ccb == nullptr) {
        goto fail;
    }

    ccb->ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
    ccb->cts.type = CTS_TYPE_CURRENT_SETTINGS;
    if (cam_send_ccb(device, ccb) < 0
        || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
        goto fail;
    }

    transport = ccb->cts.transport;

fail:
    if (device) {
        cam_close_device(device);
    }

    if (ccb) {
        cam_freeccb(ccb);
    }

    return transport;
}

/**
 * @brief Determines if a disk uses MMC, SD, or SDIO as its interface.
 */
bool isMMCSD(const struct ggeom *disk) {
    return getTransportType(disk) == XPORT_MMCSD;
}

/**
 * @brief Determines if a disk is a USB device.
 */
bool isUSB(const struct ggeom *disk) {
    return getTransportType(disk) == XPORT_USB;
}

/**
 * @brief Determines if a disk operates on the SCSI bus.
 */
bool isSCSI(const struct ggeom *disk) {
    std::array<cam_xport, 10> scsiTransports{
        XPORT_SPI,      /* SCSI Parallel Interface */
        XPORT_FC,       /* Fiber Channel */
        XPORT_SSA,      /* Serial Storage Architecture */
        XPORT_USB,      /* Universal Serial Bus */
        XPORT_ATA,      /* AT Attachment */
        XPORT_SAS,      /* Serial Attached SCSI */
        XPORT_SATA,     /* Serial AT Attachment */
        XPORT_ISCSI,    /* iSCSI */
        XPORT_SRP,      /* SCSI RDMA Protocol */
        XPORT_UFSHCI    /* Universal Flash Storage Host Interface */
    };

    cam_xport transport = getTransportType(disk);

    return std::any_of(scsiTransports.cbegin(), scsiTransports.cend(),
                  [&transport](const cam_xport& i) { return i == transport; });
}

/**
 * @brief Return the interface/bus type of a disk as a string.
 */
std::string getBusType(const struct ggeom *disk) {
    cam_xport transport = getTransportType(disk);

    switch (transport) {
    case XPORT_UNSPECIFIED:
        return "unspecified";
    case XPORT_SPI:      /* SCSI Parallel Interface */
    case XPORT_FC:       /* Fiber Channel */
    case XPORT_SSA:      /* Serial Storage Architecture */
    case XPORT_SAS:      /* Serial Attached SCSI */
    case XPORT_ISCSI:    /* iSCSI */
    case XPORT_SRP:      /* SCSI RDMA Protocol */
    case XPORT_UFSHCI:   /* Universal Flash Storage Host Interface */
        return "scsi";
    case XPORT_ATA:      /* AT Attachment */
    case XPORT_SATA:     /* Serial AT Attachment */
        return "atapi";
    case XPORT_USB:      /* Universal Serial Bus */
        return "usb";
    case XPORT_PPB:      /* Parallel Port Bus */
        return "ppb";
    case XPORT_NVME:     /* NVMe over PCIe */
    case XPORT_NVMF:     /* NVMe over Fabrics */
        return "nvme";
    case XPORT_MMCSD:    /* MMC, SD, SDIO card */
        return "mmcsd";
    case XPORT_UNKNOWN:
    default:
        return "unknown";
    }
}

/**
 * @brief Provide a description including Vendor, Model, labels, and whether it is MMCSD or loopback.
 */
std::string getDescription(const struct ggeom *gp) {
    QStringList descParts;
    descParts.reserve(4);

    // Auxiliary function for cleaning and appending to QStringList
    const auto addIfNotEmpty = [](QStringList& list, const QString& s) {
        QString trimmed = s.trimmed();
        if (!trimmed.isEmpty()) {
            list.append(trimmed);
        }
    };

    const struct gprovider *provider = LIST_FIRST(&gp->lg_provider);
    if (provider) {
        const auto description = getGeomConfig(provider, "descr");
        if (description) {
            addIfNotEmpty(descParts, *description);
        }
    }

    const auto label = getLabel(gp);
    if (label) {
        addIfNotEmpty(descParts, *label);
    }

    // Iterate over partitions and add labels to the description in the format
    // "(label1, label2, ...)"
    const auto partitionTable = getDiskPartitionTable(gp);
    if (partitionTable) {
        std::vector<const struct gprovider*> partitionObjs = getPartitionObjs(*partitionTable);
        QStringList labels;
        for (const auto& obj : partitionObjs) {
            auto label = getLabel(obj);
            if (label) {
                addIfNotEmpty(labels, *label);
            }
        }
        if (!labels.isEmpty()) {
            descParts.append("(" + labels.join(", ") + ")");
        }
    }

    if (isMMCSD(gp)) {
        descParts.append("Internal SD card reader");
    }

    std::string name = gp->lg_name;
    if (name.starts_with("lo")) {
        descParts.append("Loopback Device");
    }

    return sanitizeForDisplay(descParts.join(" ").toStdString());
}

/**
 * @brief Return the interface/bus type of a disk as a string.
 */
bool isUndesiredDeviceType(const struct ggeom *disk) {
    struct cam_device *device = cam_open_device(disk->lg_name, O_RDWR);
    if (device == nullptr) {
        return false;
    }

    std::array<uint8_t, 2> opticalTypes{T_OPTICAL, T_CDROM};
    uint8_t deviceType = SID_TYPE(&device->inq_data);

    bool undesirable = std::any_of(opticalTypes.cbegin(), opticalTypes.cend(),
                                   [&deviceType](const uint8_t i) { return i == deviceType; });

    cam_close_device(device);
    return undesirable;
}

/**
 * @brief Generate a DeviceDescriptor from a reference to the GEOM object of a disk.
 */
std::optional<DeviceDescriptor> parseDiskDevice(const struct ggeom *disk)
{
    DeviceDescriptor device;

    if (isUndesiredDeviceType(disk)) {
        return std::nullopt;
    }

    const QString name = g_device_path(disk->lg_name);
    device.device = name.toStdString();
    device.raw = name.toStdString();

    device.busType = getBusType(disk);

    device.isCard = isMMCSD(disk);
    device.isUSB = isUSB(disk);
    device.isSCSI = !device.isUSB && isSCSI(disk);
    device.isVirtual = isVirtual(disk);
    device.isReadOnly = isReadOnly(disk);
    device.isRemovable = isRemovable(disk);
    device.isSystem = device.isVirtual ? isBackingSystemPath(disk) : !device.isRemovable;

    for (const QString& mountpoint : getMountpointList(disk)) {
        device.mountpoints.push_back(mountpoint.toStdString());
    }

    for (const QString& mountpointLabel : getMountpointLabelList(disk)) {
        device.mountpointLabels.push_back(mountpointLabel.toStdString());
    }

    const struct gprovider *provider = LIST_FIRST(&disk->lg_provider);
    device.size = static_cast<uint64_t>(provider != nullptr ? provider->lg_mediasize : 0);
    device.blockSize = static_cast<uint32_t>(provider != nullptr ? provider->lg_sectorsize :0);
    device.logicalBlockSize = device.blockSize; // TODO: LOG-SEC

    device.description = getDescription(disk);

    return device;
}

} // Anonymous namespace

// ============================================================================
// Public API
// ============================================================================

std::vector<DeviceDescriptor> ListStorageDevices()
{
    const std::array<QString, 2> validDiskTypes{
        "DISK",
        "MD"
    };

    std::vector<DeviceDescriptor> deviceList;

    struct gmesh devtree;
    struct gclass *geom_class;
    struct ggeom *device;
    struct ggeom *disk;
    struct gconsumer *consumer;
    struct gprovider *provider;
    QString type;

    int error = geom_gettree(&devtree);
    if (error != 0) {
        qWarning() << "Drivelist::ListStorageDevices: Failed to open GEOM device tree"
                   << ", &devtree=" << &devtree
                   << ", errno=" << error << " (" << std::strerror(error) << ")";
        return deviceList;
    }

    LIST_FOREACH(geom_class, &devtree.lg_class, lg_class) {
        if (QString(geom_class->lg_name) != "DEV") {
            continue;
        }

        LIST_FOREACH(device, &geom_class->lg_geom, lg_geom) {
            consumer = LIST_FIRST(&device->lg_consumer);
            if (consumer == nullptr) {
                continue;
            }
            provider = consumer->lg_provider;
            if (provider == nullptr) {
                continue;
            }

            disk = provider->lg_geom;
            type = disk->lg_class->lg_name;
            if (std::none_of(validDiskTypes.cbegin(), validDiskTypes.cend(),
                             [&type](const QString& i) { return i == type; })) {
                continue;
            }

            auto deviceInfo = parseDiskDevice(disk);
            if (deviceInfo) {
                deviceList.push_back(std::move(*deviceInfo));
            }
        }

        break;
    }

    geom_deletetree(&devtree);

    return deviceList;
}

} // namespace Drivelist
