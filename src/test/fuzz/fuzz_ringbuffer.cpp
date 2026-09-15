// Fuzz the buffer between the download and the disk.
//
// Every other harness here takes bytes and hands them to a parse. This one
// drives a stateful object through a sequence of operations nobody wrote
// down: acquire, commit, acquire, release, done, cancel, reset, in whatever
// order the input asks for. The ring sits between the decompressor and the
// writer, so what comes out of it is what is written to the card.
//
// Slots may be released in any order, and each is recycled individually --
// which is the part a sequence of tests is worst at covering, because the
// interesting orders are the ones nobody thought to write. A model of what
// was committed rides alongside and says what every read must contain.
//
// Driven on one thread with a one-millisecond timeout, so a fuzzer never
// waits on a producer that will not come.
#include "ringbuffer.h"
#include "fuzz_silence.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace {

constexpr size_t kSlots = 4;
constexpr size_t kSlotSize = 64;

// What a committed slot should hold: a run derived from its sequence number,
// so a slot handed back out of order or reused too early reads as another
// slot's run rather than as anything plausible.
std::string expected(uint8_t seq, size_t length)
{
    std::string out;
    out.reserve(length);
    for (size_t i = 0; i < length; ++i)
        out.push_back(char(uint8_t(seq * 31 + i)));
    return out;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Each acquire that finds the ring full or empty waits out its timeout,
    // so the length of a sequence is most of the cost of running it. Ninety
    // six operations against four slots is long enough to fill, drain and
    // wrap several times over, and short enough to run tens of thousands of
    // sequences a minute rather than hundreds.
    if (size > 96)
        size = 96;

    RingBuffer ring(kSlots, kSlotSize, 512, 1000);

    std::deque<std::string> model;              // committed, not yet read
    std::vector<RingBuffer::Slot *> held;       // read slots not released yet
    uint8_t seq = 0;
    bool done = false;

    for (size_t i = 0; i < size; ++i) {
        switch (data[i] % 6) {
        case 0:
        case 1: {   // produce
            if (done)
                break;
            // The model knows whether a slot is owed. Waiting out a timeout
            // for the full case on every other operation costs more than it
            // finds, so it is reached one time in eight -- and when a slot
            // is owed, not getting one is itself the finding.
            const bool owed = model.size() + held.size() < kSlots;
            if (!owed && (data[i] & 7) != 0)
                break;
            RingBuffer::Slot *slot = ring.acquireWriteSlot(1);
            if (!slot) {
                if (owed && !ring.isCancelled())
                    __builtin_trap();   // a free slot the ring would not hand over
                break;
            }
            if (!slot->data || slot->capacity != kSlotSize)
                __builtin_trap();
            const size_t length = size_t(data[i]) % (kSlotSize + 1);
            const std::string payload = expected(seq, length);
            if (length > 0)
                memcpy(slot->data, payload.data(), length);
            ring.commitWriteSlot(slot, length);
            model.push_back(payload);
            ++seq;
            if (model.size() + held.size() > kSlots)
                __builtin_trap();   // more live slots than the ring owns
            break;
        }

        case 2:
        case 3: {   // consume
            if (model.empty() && (data[i] & 7) != 0)
                break;              // the empty case, one time in eight
            RingBuffer::Slot *slot = ring.acquireReadSlot(1);
            if (!slot) {
                // Nothing to read is only allowed when nothing is waiting.
                if (!model.empty() && !ring.isCancelled())
                    __builtin_trap();
                break;
            }
            if (model.empty())
                __builtin_trap();   // handed data nobody committed
            const std::string &want = model.front();
            if (slot->size != want.size())
                __builtin_trap();
            if (want.size() > 0 && memcmp(slot->data, want.data(), want.size()) != 0)
                __builtin_trap();   // out of order, or a slot reused too early
            model.pop_front();
            held.push_back(slot);
            break;
        }

        case 4: {   // release one of the held slots, oldest or newest
            if (held.empty())
                break;
            const size_t which = (data[i] & 0x40) ? held.size() - 1 : 0;
            ring.releaseReadSlot(held.at(which));
            held.erase(held.begin() + long(which));
            break;
        }

        default: {  // the ends of a transfer
            switch ((data[i] >> 3) % 3) {
            case 0:
                ring.producerDone();
                done = true;
                break;
            case 1:
                ring.cancel();
                done = true;
                break;
            default:
                // reset() takes the ring back to empty, so the model goes
                // with it -- and so does anything still held, which reset
                // reclaims.
                ring.reset();
                model.clear();
                held.clear();
                done = false;
                seq = 0;
                break;
            }
            break;
        }
        }

        if (ring.numSlots() != kSlots || ring.slotCapacity() != kSlotSize)
            __builtin_trap();
    }

    // Drain what is left, still in order.
    for (auto *slot : held)
        ring.releaseReadSlot(slot);
    held.clear();

    if (!ring.isCancelled()) {
        while (!model.empty()) {
            RingBuffer::Slot *slot = ring.acquireReadSlot(1);
            if (!slot)
                __builtin_trap();   // committed and then lost
            const std::string &want = model.front();
            if (slot->size != want.size())
                __builtin_trap();
            if (want.size() > 0 && memcmp(slot->data, want.data(), want.size()) != 0)
                __builtin_trap();
            model.pop_front();
            ring.releaseReadSlot(slot);
        }
    }

    return 0;
}
