/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Shared timeout for fixture subprocesses (mkfs.vfat, openssl, tar, ...).
 */

#ifndef RPI_TEST_FIXTURE_PROCESS_H
#define RPI_TEST_FIXTURE_PROCESS_H

namespace rpi_test {

// How long to wait for a helper process that *builds a fixture*, as opposed
// to one whose duration is itself under test.
constexpr int kFixtureProcessTimeoutMs = 300000;

} // namespace rpi_test

#endif // RPI_TEST_FIXTURE_PROCESS_H
