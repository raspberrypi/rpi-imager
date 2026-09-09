/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Shared timeout for fixture subprocesses (mkfs.vfat, openssl, tar, ...).
 */

#ifndef RPI_TEST_FIXTURE_PROCESS_H
#define RPI_TEST_FIXTURE_PROCESS_H

namespace rpi_test {

// How long to wait for a helper process that *builds a fixture*, as opposed
// to one whose duration is itself under test.
//
// These are near-instant in isolation -- mkfs.vfat on a 48 MB image, openssl
// generating a key -- but the suite runs under `ctest -j8`, and on a
// saturated machine one has been observed to take over a minute. With a 60 s
// wait that surfaced as a failing test, which is misleading: nothing was
// wrong with the code under test, the fixture just had not finished being
// built. The cap is therefore set well beyond any plausible honest runtime;
// a genuinely hung process is still caught by ctest's own per-test timeout,
// which is the right mechanism for that.
constexpr int kFixtureProcessTimeoutMs = 300000;

} // namespace rpi_test

#endif // RPI_TEST_FIXTURE_PROCESS_H
