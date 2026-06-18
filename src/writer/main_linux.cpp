// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// rpi-imager-writer: the privileged Linux helper.
//
// Launched via pkexec on demand by the unprivileged client (phase 2). Passed
// a Unix-domain socket path on the command line; exits when the client
// disconnects. Qt-free (§8a).

#include "server/linux/service_impl.h"

int main(int argc, char** argv) {
    return rpi_imager::writer::RpiImagerWriterServiceMainLinux(argc, argv);
}
