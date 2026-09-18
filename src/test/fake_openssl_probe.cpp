/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * A stand-in for openssl, for the cases about it not co-operating.
 *
 * Those cases used to plant a shell script named "openssl" with a shebang and
 * no extension. Windows will not run that: CreateProcess appends ".exe" to a
 * bare name and nothing else, so the script was never found and the cases
 * passed by taking the "openssl is not installed" path instead of the one
 * they are named after. A real executable is found on every platform.
 *
 * What it does is chosen by RPI_FAKE_OPENSSL:
 *
 *   succeed      exit 0 having written and printed nothing
 *   genrsa-only  write the file `genrsa -out <path>` asks for, refuse the rest
 *
 * Anything else, including the variable being unset, is refused -- a stray
 * copy on someone's PATH should do nothing at all.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv)
{
    const char *mode = std::getenv("RPI_FAKE_OPENSSL");
    if (!mode)
        mode = "";

    if (std::strcmp(mode, "succeed") == 0)
        return 0;

    if (std::strcmp(mode, "genrsa-only") == 0) {
        // `openssl genrsa -out <path> 2048`
        if (argc >= 4 && std::strcmp(argv[1], "genrsa") == 0) {
            std::FILE *f = std::fopen(argv[3], "wb");
            if (!f) {
                std::fprintf(stderr, "fake openssl: cannot write %s\n", argv[3]);
                return 1;
            }
            std::fclose(f);
            return 0;
        }
        std::fprintf(stderr, "fake openssl: refusing\n");
        return 1;
    }

    std::fprintf(stderr, "fake openssl: no behaviour selected\n");
    return 1;
}
