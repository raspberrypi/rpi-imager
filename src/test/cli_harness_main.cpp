/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The command line, in a binary a test can actually start on Windows.
 *
 * The shipping manifest asks for requireAdministrator, so CreateProcess
 * refuses outright from an unelevated process -- QProcess reports the start as
 * never having happened. Every case in cli_process_test therefore skipped, and
 * cli.cpp was the least covered file in the tree despite being a shipping
 * entry point that script authors meet first.
 *
 * This carries no manifest and links the same Cli out of the same library, so
 * run() is the code that ships. What it does not reproduce is the elevation
 * the manifest arranges, which cli_admin_fault.cpp answers instead.
 */

#include "cli.h"

int main(int argc, char *argv[])
{
    // The two lines main.cpp runs for --cli, and nothing else: anything added
    // here would be behaviour the shipping binary does not have.
    Cli cli(argc, argv);
    return cli.run();
}
