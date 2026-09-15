// Fuzz the bootloader EEPROM image parser.
//
// pieeprom.bin is a sequence of sections, each carrying its own magic and
// length, and the image arrives as a firmware download. A length that
// disagrees with the buffer is the whole hazard, so parse() is driven first
// and then everything a caller does with what it produced.
#include "fastboot/pieeprom.h"
#include "fuzz_silence.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 2 * 1024 * 1024)
        return 0;

    fastboot::pieeprom::Image img(std::vector<uint8_t>(data, data + size));
    const std::string err = img.parse();
    if (!err.empty())
        return 0;

    // Accepted. Every section it accepted has to describe a range inside the
    // image: parse() refuses a file section shorter than its own header and
    // one running past the end, and it is the first of those that stops the
    // second being asked about a number that wrapped -- contentSize() is
    // length - 16 computed in size_t, so a length of 8 becomes enormous.
    for (const auto &s : img.sections()) {
        if (s.contentOffset() > size || s.contentSize() > size
            || s.contentOffset() + s.contentSize() > size)
            __builtin_trap();          // a section pointing outside the image
        if (s.isFile())
            (void)img.readFile(s.filename);
    }
    (void)img.findFile("bootconf.txt");
    (void)img.readFile("bootconf.txt");
    (void)img.readBootConfText();

    // And the text parser that runs on a section's contents.
    const std::string_view conf(reinterpret_cast<const char *>(data), size);
    (void)fastboot::pieeprom::parseBootOrder(conf);

    // The boot order the imager sets is the one the Pi uses next, and it is
    // set by rewriting a line of text in a section of the EEPROM. So the
    // property that matters is not that the write succeeds but that it is
    // the value that comes back: a bootconf carrying the line twice, or
    // carrying something that only looks like it, would leave the device
    // booting from somewhere nobody chose.
    for (uint32_t wanted : { 0x1u, 0x21u, 0xf41u, 0xffffffffu }) {
        const std::string updated =
            fastboot::pieeprom::setBootOrderLine(conf, wanted);
        const auto readBack = fastboot::pieeprom::parseBootOrder(updated);
        if (!readBack)
            __builtin_trap();              // written, and then unreadable
        if (*readBack != wanted)
            __builtin_trap();              // written, and read as another
    }

    // The write side, which this target never drove: writeFile(),
    // writeBootConfText() and writeBootConfSig() were three entry points
    // nobody had given it. writeFile() rewrites a section in place --
    // recomputing its length, padding to an 8-byte boundary, inserting a
    // PAD_MAGIC section to fill the gap -- and its contract is explicit:
    // an empty string means it worked. So what comes back has to be what
    // went in. Left until last because it mutates the image the checks
    // above read.
    {
        const std::vector<uint8_t> body(1 + (size % 64), 0x41);
        if (img.writeFile("bootconf.txt", body).empty()) {
            const auto back = img.readFile("bootconf.txt");
            if (!back || *back != body)
                __builtin_trap();          // written, and read as something else
        }
    }
    {
        const std::string text(1 + (size % 32), 'x');
        if (img.writeBootConfText(text).empty()) {
            const auto back = img.readBootConfText();
            if (!back || *back != text)
                __builtin_trap();          // the text set is not the text read
        }
    }
    return 0;
}
