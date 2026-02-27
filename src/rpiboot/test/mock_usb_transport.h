/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Mock USB transport for unit-testing the rpiboot protocol without
 * real hardware.  Allows tests to queue scripted bulk-IN responses
 * and capture bulk-OUT / control-transfer calls for assertion.
 */

#ifndef RPIBOOT_MOCK_USB_TRANSPORT_H
#define RPIBOOT_MOCK_USB_TRANSPORT_H

#include "../usb_transport.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <span>
#include <vector>

namespace rpiboot::testing {

struct ControlTransferRecord {
    uint8_t requestType;
    uint8_t request;
    uint16_t wValue;
    uint16_t wIndex;
    std::vector<uint8_t> data;
};

class MockUsbTransport : public IUsbTransport {
public:
    // ── Scripting helpers ──────────────────────────────────────────────

    // Queue a response that will be returned by the next bulkRead call
    void queueBulkReadResponse(std::vector<uint8_t> data)
    {
        _bulkReadQueue.push_back(std::move(data));
    }

    // Set whether the transport reports as open
    void setOpen(bool open) { _isOpen = open; }

    // Configure a simulated failure on the next N bulk writes
    void failNextBulkWrites(int count) { _failBulkWriteCount = count; }

    // Configure a simulated failure on the next N control transfers
    void failNextControlTransfers(int count) { _failControlCount = count; }

    // ── Captured output ────────────────────────────────────────────────

    const std::vector<std::vector<uint8_t>>& capturedBulkWrites() const
    {
        return _capturedBulkWrites;
    }

    const std::vector<ControlTransferRecord>& capturedControlTransfers() const
    {
        return _capturedControlTransfers;
    }

    // ── IUsbTransport implementation ───────────────────────────────────

    bool controlTransfer(uint8_t requestType, uint8_t request,
                         uint16_t wValue, uint16_t wIndex,
                         std::span<const uint8_t> data,
                         int /*timeoutMs*/) override
    {
        if (_failControlCount > 0) {
            --_failControlCount;
            return false;
        }

        ControlTransferRecord rec;
        rec.requestType = requestType;
        rec.request = request;
        rec.wValue = wValue;
        rec.wIndex = wIndex;
        rec.data.assign(data.begin(), data.end());
        _capturedControlTransfers.push_back(std::move(rec));
        return true;
    }

    int bulkWrite(uint8_t /*endpoint*/,
                  std::span<const uint8_t> data,
                  int /*timeoutMs*/) override
    {
        if (_failBulkWriteCount > 0) {
            --_failBulkWriteCount;
            return -1;
        }

        _capturedBulkWrites.emplace_back(data.begin(), data.end());
        return static_cast<int>(data.size());
    }

    int bulkRead(uint8_t /*endpoint*/,
                 std::span<uint8_t> buffer,
                 int /*timeoutMs*/) override
    {
        if (_bulkReadQueue.empty())
            return -1;

        auto& front = _bulkReadQueue.front();
        size_t toCopy = std::min(front.size(), buffer.size());
        std::memcpy(buffer.data(), front.data(), toCopy);
        _bulkReadQueue.pop_front();
        return static_cast<int>(toCopy);
    }

    bool isOpen() const override { return _isOpen; }

private:
    bool _isOpen = true;
    int _failBulkWriteCount = 0;
    int _failControlCount = 0;
    std::deque<std::vector<uint8_t>> _bulkReadQueue;
    std::vector<std::vector<uint8_t>> _capturedBulkWrites;
    std::vector<ControlTransferRecord> _capturedControlTransfers;
};

} // namespace rpiboot::testing

#endif // RPIBOOT_MOCK_USB_TRANSPORT_H
