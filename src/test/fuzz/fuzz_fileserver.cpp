// Fuzz the filename a booting device asks the rpiboot file server for.
//
// The name arrives over USB from the device, and anything that speaks the
// protocol chooses it. The caller checks only that the characters are
// printable ASCII, which "../../etc/shadow" satisfies. Whatever is opened
// goes straight back to the device, and the imager is often running elevated
// so that it can write to block devices.
//
// There is no parse here, so the subject is the guard. The tree below plants
// a secret outside the served directory, and inside it a symlink pointing at
// that secret -- which the table-driven cases in rpiboot_protocol_test do not
// cover. The property is that no byte of it ever comes back, whatever name
// the device sends.
#include "rpiboot/file_server.h"
#include "fuzz_silence.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char kSecret[] = "RPIBOOT-FUZZ-SECRET-must-never-be-served";
const char kPlain[]  = "firmware-bytes";
const char kNested[] = "chip-specific-bytes";

struct Tree {
    std::filesystem::path root;
    std::filesystem::path firmware;
};

const Tree &tree()
{
    static const Tree t = [] {
        char templ[] = "/tmp/rpi-fuzz-fileserver-XXXXXX";
        const char *made = ::mkdtemp(templ);
        if (!made)
            __builtin_trap();

        Tree out;
        out.root = made;
        out.firmware = out.root / "firmware";
        std::filesystem::create_directories(out.firmware / "2712");

        std::ofstream(out.root / "secret.txt") << kSecret;
        std::ofstream(out.firmware / "bootcode4.bin") << kPlain;
        std::ofstream(out.firmware / "2712" / "bootcode5.bin") << kNested;

        std::error_code ec;
        std::filesystem::create_symlink(out.root / "secret.txt",
                                        out.firmware / "escape", ec);
        std::filesystem::create_symlink(out.root, out.firmware / "up", ec);

        static const std::filesystem::path toRemove = out.root;
        std::atexit([] {
            std::error_code rc;
            std::filesystem::remove_all(toRemove, rc);
        });
        return out;
    }();
    return t;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const Tree &t = tree();
    if (size > 2048)
        size = 2048;

    // Printable ASCII is the whole domain: run() drops a message whose
    // filename holds anything else before this is reached, so a finding
    // outside it would be one that cannot happen.
    std::string name;
    name.reserve(size);
    for (size_t i = 0; i < size; ++i)
        name.push_back(char(0x20 + (data[i] % 0x5F)));

    const std::vector<uint8_t> out =
        rpiboot::FileServer::readFileFromDisk(t.firmware, name);
    if (out.empty())
        return 0;

    const std::string served(out.begin(), out.end());
    if (served.find(kSecret) != std::string::npos)
        __builtin_trap();   // the served directory was escaped
    if (served != kPlain && served != kNested)
        __builtin_trap();   // something nobody planted came back
    return 0;
}
