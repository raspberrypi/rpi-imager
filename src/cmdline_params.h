/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The kernel parameters and cloud-init identifiers a customised first boot
 * needs, in one place.
 *
 * Two write paths append these: downloadthread.cpp for a card, and
 * fastbootflashthread.cpp for a device in fastboot mode. Each carried its
 * own copy. They agreed, which is the state worth worrying about -- a change
 * to one is invisible from the other, and each path's tests assert only its
 * own string, so nothing in the suite compares them. Sharing the text means
 * they cannot drift rather than testing that they have not.
 */

#ifndef CMDLINE_PARAMS_H
#define CMDLINE_PARAMS_H

#include <QByteArray>
#include <QDateTime>

namespace rpi_cmdline {

/*
 * systemd's first-boot runner.
 *
 * run_success_action=reboot is what makes the second boot the user's own;
 * kernel-command-line.target is what puts it before anything that wants the
 * network. Written as one literal because a reader has to see the whole
 * parameter list to judge it.
 */
inline QByteArray systemdFirstRun()
{
    return QByteArrayLiteral(" systemd.run=/boot/firstrun.sh"
                             " systemd.run_success_action=reboot"
                             " systemd.unit=kernel-command-line.target");
}

/* A NoCloud instance id, unique per write. */
inline QByteArray newInstanceId()
{
    return "rpi-imager-" + QByteArray::number(QDateTime::currentMSecsSinceEpoch());
}

/* The meta-data file the NoCloud datasource needs in order to be detected. */
inline QByteArray nocloudMetaData(const QByteArray &instanceId)
{
    return "instance-id: " + instanceId + "\n";
}

/*
 * Datasource and instance on the kernel command line.
 *
 * cloud-init's check_instance_id() validates its cache from this rather than
 * from seed_dirs, which are never populated for this deployment pattern.
 * Without it the cache is invalidated on every reboot -- /run is tmpfs -- and
 * the whole of /boot/firmware is rediscovered each time.
 */
inline QByteArray nocloudDatasource(const QByteArray &instanceId)
{
    return " ds=nocloud;i=" + instanceId;
}

} // namespace rpi_cmdline

#endif // CMDLINE_PARAMS_H
