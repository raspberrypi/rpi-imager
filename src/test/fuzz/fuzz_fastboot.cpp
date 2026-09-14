// Fuzz the fastboot response parser against a device that says anything.
//
// Everything here arrives over bulk IN from hardware plugged into the
// machine, so the bytes are entirely the device's choice. The transport
// below hands the fuzzer's buffer back a packet at a time, which is what a
// real device does -- INFO lines before a terminal OKAY or FAIL, and a DATA
// response carrying a size the caller is expected to believe.
#include "fastboot/fastboot_protocol.h"
#include "rpiboot/usb_transport.h"
#include "fuzz_silence.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <span>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

class ScriptedTransport : public rpiboot::IUsbTransport
{
public:
    explicit ScriptedTransport(const uint8_t *data, size_t size)
        : _data(data), _size(size) {}

    bool controlTransfer(uint8_t, uint8_t, uint16_t, uint16_t,
                         std::span<const uint8_t>, int) override { return true; }
    int controlTransferIn(uint8_t, uint8_t, uint16_t, uint16_t,
                          std::span<uint8_t>, int) override { return 0; }
    // The full length, not zero. Every entry point checks the command it
    // sent was written whole and gives up otherwise, so a transport that
    // reports nothing written meant none of them ever reached a response --
    // the corpus stopped at two inputs of three bytes between them, and the
    // seeds below were all rejected as uninteresting.
    int bulkWrite(uint8_t, std::span<const uint8_t> s, int) override
    {
        return int(s.size());
    }

    // Each read takes the next slice, sized by a length byte from the input
    // so packet boundaries fall where the fuzzer puts them.
    int bulkRead(uint8_t, std::span<uint8_t> buffer, int) override
    {
        if (_pos >= _size)
            return -1;
        size_t want = size_t(_data[_pos++]) + 1;
        want = std::min({want, buffer.size(), _size - _pos});
        if (want == 0)
            return -1;
        std::memcpy(buffer.data(), _data + _pos, want);
        _pos += want;
        return int(want);
    }

    bool isOpen() const override { return true; }

private:
    const uint8_t *_data;
    size_t _size;
    size_t _pos = 0;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2 || size > 64 * 1024)
        return 0;

    // Wrapped, because production is. readResponse() parses the size of a
    // DATA response with std::stoul, which throws invalid_argument when the
    // eight characters are not hex -- "DATAzzzzzzzz" from a device that says
    // anything. Every call site in the tree sits inside
    // FastbootFlashThread::run()'s catch-all, so it surfaces as an error
    // rather than a crash; an unguarded harness just rediscovers that on its
    // first seed and explores nothing past it.
    // dataSize is documented as meaningful only for a Data response, so a
    // number left on any other kind is one a caller may act on: download()
    // and writeDeviceFile() size their transfers from it, and the device
    // chose it.
    const auto checkResponse = [](const fastboot::Response &r) {
        if (r.type != fastboot::Response::Data && r.dataSize != 0)
            __builtin_trap();
    };

    // Every one of these is documented to set lastError() when it fails. A
    // failure with nothing to say is one the interface reports as a blank,
    // and the user is left with a write that stopped for no stated reason.
    // A fresh protocol per call, so a message left by the last one cannot
    // stand in for a missing one.
    const auto checkFailure = [](const fastboot::FastbootProtocol &p, bool ok) {
        if (!ok && p.lastError().empty())
            __builtin_trap();
    };

    fastboot::FastbootProtocol proto;
    try {
    {
        ScriptedTransport t(data, size);
        const fastboot::Response r = proto.sendCommand(t, "getvar:product", 10);
        checkResponse(r);
        (void)r.message.size();
    }
    {
        // A second pass with a different command, since which terminal
        // response the parser is waiting for changes what it accepts.
        ScriptedTransport t(data, size);
        checkResponse(proto.sendCommand(t, "download:00100000", 10));
    }

    // sendCommand() is one of about ten entry points reading a device's
    // answer, and was the only one driven here -- which is why the corpus
    // stopped growing at seven inputs. The rest interpret replies of their
    // own: download() takes a DATA size, the mount and eeprom calls an OKAY
    // or FAIL per step. Nothing here touches hardware.
    std::atomic<bool> cancelled{false};
    const std::span<const uint8_t> payload(data, std::min<size_t>(size, 4096));

    {
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        checkFailure(p, p.download(t, payload, nullptr, cancelled));
    }
    {
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        checkFailure(p, p.mountDevice(t, "/dev/mmcblk0p1", "/mnt", "vfat"));
    }
    {
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        checkFailure(p, p.umountDevice(t, "/mnt"));
    }
    {
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        checkFailure(p, p.writeDeviceFile(t, "/mnt/config.txt", payload, cancelled));
    }
    {
        // The one entry point here whose *result* a caller then acts on
        // rather than merely checking. config.txt and cmdline.txt are read
        // back through this and edited, and two defects lived in what the
        // caller did with the answer: a device returning a padded read had
        // its padding merged into config.txt and written back, and had the
        // kernel command line appended behind it, where the kernel never
        // reads. Both are fixed in the caller; this covers the read itself.
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        const std::vector<uint8_t> got =
            p.readDeviceFile(t, "/mnt/cmdline.txt", cancelled);

        // Empty means failure and sets lastError(); anything else is content
        // the caller will believe. What it must not be is longer than the
        // device could have sent, which is the whole of the fuzzer's buffer.
        if (!got.empty()) {
            if (got.size() > size)
                __builtin_trap();
            (void)p.lastError().size();
        }
    }
    {
        // The other read whose answer a caller acts on: the EEPROM comes
        // back this way and is then parsed. It shares upload() with
        // readDeviceFile, which is where a device declaring four gigabytes
        // was making us set them aside -- so the same bound is asked of it.
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        const std::vector<uint8_t> eeprom = p.readEeprom(t, "", nullptr, cancelled);
        if (!eeprom.empty() && eeprom.size() > size)
            __builtin_trap();
    }
    {
        // Staging and flashing read their own replies. Nothing is asserted
        // about the outcome -- a device that refuses is an ordinary answer
        // -- only that whatever it says is survivable.
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        checkFailure(p, p.stage(t, payload, nullptr, cancelled));
    }
    {
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        checkFailure(p, p.flash(t, "boot", 10));
    }
    {
        // Verifying compares a read-back against what was written, so the
        // device chooses both sides of the comparison.
        fastboot::FastbootProtocol p;
        ScriptedTransport t(data, size);
        checkFailure(p, p.verifyEeprom(t, payload, "", nullptr, cancelled));
    }
    } catch (const std::exception &) {
    } catch (...) {
    }
    return 0;
}
