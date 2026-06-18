// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Qt-free OS drive-change watcher for privileged helper push (§5).

#pragma once

#include <functional>

namespace rpi_imager::writer {

class DriveWatchService {
public:
    using NotifyFn = std::function<void()>;

    DriveWatchService();
    ~DriveWatchService();

    DriveWatchService(const DriveWatchService&) = delete;
    DriveWatchService& operator=(const DriveWatchService&) = delete;

    void subscribe(NotifyFn fn);
    void unsubscribe();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace rpi_imager::writer
