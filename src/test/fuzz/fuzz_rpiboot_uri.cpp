// Fuzz the rpiboot device URI parser.
//
// The URI names a bus, an address and a chip generation, and it is handed
// around as a string between the scanner, the UI and the CLI -- so it is
// reconstructed from text rather than carried as a struct. The field split
// is hand-rolled, which is the part worth hammering.
#include "rpiboot/rpiboot_types.h"
#include "fuzz_silence.h"

#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096)
        return 0;

    const std::string_view uri(reinterpret_cast<const char *>(data), size);
    const rpiboot::DeviceUri parsed = rpiboot::parseDeviceUri(uri);
    (void)parsed.valid;
    (void)parsed.busNumber;
    (void)parsed.deviceAddress;

    (void)rpiboot::uriFieldToNumber(uri);
    if (size >= 2) {
        const uint16_t pid = uint16_t(data[0]) | uint16_t(uint16_t(data[1]) << 8);
        (void)rpiboot::chipGenerationFromPid(pid);
    }

    // The URI exists to carry a device between the scanner, the interface
    // and the command line, so what is written has to read back as what was
    // written. A field lost or transposed on that trip is a different device
    // -- and the thing on the other end of it is flashed.
    //
    // Only a URI that parsed is round-tripped: the formatter is given fields,
    // not text, so there is nothing to say about one that did not.
    if (parsed.valid) {
        // The formatter takes a generation rather than an optional one, so a
        // URI that named none is rebuilt with a stand-in and only the three
        // fields it did name are compared.
        const bool named = parsed.chipGeneration.has_value();
        const rpiboot::ChipGeneration generation =
            named ? *parsed.chipGeneration : rpiboot::ChipGeneration::BCM2712;
        const std::string rebuilt =
            rpiboot::formatDeviceUri(parsed.busNumber, parsed.deviceAddress,
                                     parsed.portPath, generation);
        const rpiboot::DeviceUri again = rpiboot::parseDeviceUri(rebuilt);

        if (!again.valid)
            __builtin_trap();                    // written, and unreadable
        if (again.busNumber != parsed.busNumber)
            __builtin_trap();
        if (again.deviceAddress != parsed.deviceAddress)
            __builtin_trap();
        if (again.portPath != parsed.portPath)
            __builtin_trap();                    // a different port on the hub
        if (again.chipGeneration != generation)
            __builtin_trap();
    }
    return 0;
}
